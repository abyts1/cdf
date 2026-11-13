// cdf-scan: fast parallel directory lister for cdf.
// Prints every directory under ROOT (excluding ROOT itself), one per line,
// skipping pruned names. Does not follow symlinks. Output order is unspecified.
//
// Speed comes from three things:
//  - Directories are opened with openat() relative to their parent's open fd,
//    so the kernel never re-walks the full path, and threads don't contend on
//    the shared prefix (/home/you/...) during lookup.
//  - Entries are read with raw getdents64 into a large stack buffer.
//  - A worker keeps one child to scan itself and only queues the rest,
//    which cuts traffic on the shared work queue.
#define _GNU_SOURCE
#include <dirent.h>  // DT_DIR, DT_UNKNOWN
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static const char *PRUNE[] = {".git", "node_modules", ".cache", ".wine", "__pycache__",
                              ".npm", ".cargo", ".rustup", ".local", "venv", ".venv", NULL};

// A directory waiting to be scanned. It is opened relative to `parent` when
// parent holds an open fd, otherwise by its full path.
typedef struct node {
    struct node *parent;
    atomic_int refs;   // 1 for "not yet scanned" + 1 per child still using our fd
    int fd;            // our open fd while children may still need it, else -1
    size_t len;        // strlen(path)
    const char *name;  // points into path, after the last '/'
    char path[];
} node;

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

static node **work;
static size_t work_len, work_cap, busy;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t out_mu = PTHREAD_MUTEX_INITIALIZER;
static atomic_long open_fds;
static long fd_cap;  // above this many held fds, children fall back to full paths

static int pruned(const char *n) {
    for (const char **p = PRUNE; *p; p++) if (strcmp(n, *p) == 0) return 1;
    return 0;
}

// Drop one reference; free the node (closing its fd) when it was the last,
// and walk up to release the reference it held on its own parent.
static void release(node *n) {
    while (n && atomic_fetch_sub(&n->refs, 1) == 1) {
        node *p = n->parent;
        if (n->fd >= 0) { close(n->fd); atomic_fetch_sub(&open_fds, 1); }
        free(n);
        n = p;
    }
}

typedef struct { char buf[1 << 16]; size_t n; } outbuf;

static void flush(outbuf *o) {
    if (!o->n) return;
    pthread_mutex_lock(&out_mu);
    fwrite(o->buf, 1, o->n, stdout);
    pthread_mutex_unlock(&out_mu);
    o->n = 0;
}

static void emit(outbuf *o, const char *s, size_t len) {
    if (o->n + len + 1 > sizeof o->buf) flush(o);
    memcpy(o->buf + o->n, s, len);
    o->buf[o->n + len] = '\n';
    o->n += len + 1;
}

static node *new_node(node *parent, const char *name, size_t nlen) {
    size_t plen = parent->len, len = plen + 1 + nlen;
    node *c = malloc(sizeof *c + len + 1);
    if (!c) { perror("malloc"); exit(1); }
    memcpy(c->path, parent->path, plen);
    c->path[plen] = '/';
    memcpy(c->path + plen + 1, name, nlen + 1);
    c->len = len;
    c->name = c->path + plen + 1;
    c->parent = parent;
    c->fd = -1;
    atomic_init(&c->refs, 1);
    return c;
}

// Scan one directory. Returns a child for the caller to scan next (or NULL).
static node *scan_dir(node *n, outbuf *o, node ***kids, size_t *kcap) {
    int fd;
    node *p = n->parent;
    if (p && p->fd >= 0)
        fd = openat(p->fd, n->name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    else
        fd = open(n->path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    n->parent = NULL;
    release(p);  // the parent no longer needs to keep its fd open for us
    if (fd < 0) { release(n); return NULL; }  // unreadable: skip quietly

    char buf[1 << 15];
    size_t nk = 0;
    long r;
    while ((r = syscall(SYS_getdents64, fd, buf, sizeof buf)) > 0) {
        for (long off = 0; off < r;) {
            struct linux_dirent64 *e = (void *)(buf + off);
            off += e->d_reclen;
            const char *nm = e->d_name;
            if (nm[0] == '.' && (!nm[1] || (nm[1] == '.' && !nm[2]))) continue;
            int isdir;
            if (e->d_type == DT_DIR) isdir = 1;
            else if (e->d_type == DT_UNKNOWN) {
                struct stat st;
                isdir = fstatat(fd, nm, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(st.st_mode);
            } else isdir = 0;
            if (!isdir || pruned(nm)) continue;
            node *c = new_node(n, nm, strlen(nm));
            emit(o, c->path, c->len);
            if (nk == *kcap) {
                *kcap = *kcap ? *kcap * 2 : 64;
                *kids = realloc(*kids, *kcap * sizeof **kids);
                if (!*kids) { perror("realloc"); exit(1); }
            }
            (*kids)[nk++] = c;
        }
    }

    // Keep our fd open for the children, unless we're already holding too many.
    if (nk && atomic_load(&open_fds) < fd_cap) {
        n->fd = fd;
        atomic_fetch_add(&open_fds, 1);
        atomic_fetch_add(&n->refs, (int)nk);
    } else {
        close(fd);
        for (size_t i = 0; i < nk; i++) (*kids)[i]->parent = NULL;
    }
    node *mine = nk ? (*kids)[nk - 1] : NULL;
    if (nk > 1) {
        pthread_mutex_lock(&mu);
        if (work_len + nk > work_cap) {
            while (work_len + nk > work_cap) work_cap *= 2;
            work = realloc(work, work_cap * sizeof *work);
            if (!work) { perror("realloc"); exit(1); }
        }
        memcpy(work + work_len, *kids, (nk - 1) * sizeof *work);
        work_len += nk - 1;
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&mu);
    }
    release(n);  // drop the "not yet scanned" reference
    return mine;
}

static void *worker(void *arg) {
    (void)arg;
    outbuf *o = malloc(sizeof *o);
    node **kids = NULL;
    size_t kcap = 0;
    o->n = 0;
    pthread_mutex_lock(&mu);
    for (;;) {
        while (work_len == 0 && busy > 0) pthread_cond_wait(&cv, &mu);
        if (work_len == 0) break;  // queue empty and nobody can add more: done
        node *n = work[--work_len];
        busy++;
        pthread_mutex_unlock(&mu);
        while (n) n = scan_dir(n, o, &kids, &kcap);
        pthread_mutex_lock(&mu);
        if (--busy == 0 && work_len == 0) pthread_cond_broadcast(&cv);
    }
    pthread_mutex_unlock(&mu);
    flush(o);
    free(o);
    free(kids);
    return NULL;
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : getenv("HOME");
    if (!root) { fprintf(stderr, "usage: cdf-scan [ROOT]\n"); return 2; }

    // Raise the soft fd limit to the hard limit; hold at most half of it.
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        if (rl.rlim_cur < rl.rlim_max) { rl.rlim_cur = rl.rlim_max; setrlimit(RLIMIT_NOFILE, &rl); }
        getrlimit(RLIMIT_NOFILE, &rl);
        fd_cap = rl.rlim_cur == RLIM_INFINITY ? 65536 : (long)rl.rlim_cur / 2 - 64;
        if (fd_cap > 65536) fd_cap = 65536;
    }
    if (fd_cap < 0) fd_cap = 0;

    size_t rlen = strlen(root);
    while (rlen > 1 && root[rlen - 1] == '/') rlen--;
    node *r = malloc(sizeof *r + rlen + 1);
    if (!r) { perror("malloc"); return 1; }
    memcpy(r->path, root, rlen);
    r->path[rlen] = 0;
    r->len = rlen;
    r->name = r->path;
    r->parent = NULL;
    r->fd = -1;
    atomic_init(&r->refs, 1);
    work_cap = 1024;
    work = malloc(work_cap * sizeof *work);
    if (!work) { perror("malloc"); return 1; }
    work[work_len++] = r;

    long n = sysconf(_SC_NPROCESSORS_ONLN);
    const char *env = getenv("CDF_SCAN_THREADS");
    if (env) n = atol(env);
    else if (n > 10) n = 10;  // fastest on a 32-core WSL2 box; more threads contend in the kernel
    if (n < 1) n = 1;
    if (n > 64) n = 64;
    pthread_t t[64];
    for (long i = 0; i < n; i++) pthread_create(&t[i], NULL, worker, NULL);
    for (long i = 0; i < n; i++) pthread_join(t[i], NULL);
    return 0;
}
