// cdf-scan: fast parallel directory lister for cdf.
// Prints every directory under ROOT (excluding ROOT itself), one per line,
// skipping pruned names. Does not follow symlinks. Output order is unspecified.
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *PRUNE[] = {".git", "node_modules", ".cache", ".wine", "__pycache__",
                              ".npm", ".cargo", ".rustup", ".local", "venv", ".venv", NULL};

typedef struct { char **items; size_t len, cap; } stack_t_;
static stack_t_ work;
static size_t busy;  // threads currently processing a directory
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t out_mu = PTHREAD_MUTEX_INITIALIZER;

static int pruned(const char *n) {
    for (const char **p = PRUNE; *p; p++) if (strcmp(n, *p) == 0) return 1;
    return 0;
}

static void push_locked(char *path) {
    if (work.len == work.cap) {
        work.cap = work.cap ? work.cap * 2 : 1024;
        work.items = realloc(work.items, work.cap * sizeof *work.items);
        if (!work.items) { perror("realloc"); exit(1); }
    }
    work.items[work.len++] = path;
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

static void scan_dir(char *path, outbuf *o) {
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;  // permission denied etc: skip quietly, like find 2>/dev/null
    DIR *d = fdopendir(fd);
    if (!d) { close(fd); return; }
    size_t plen = strlen(path);
    // local batch so we take the queue lock once per directory, not per child
    char **kids = NULL; size_t nk = 0, ck = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *n = e->d_name;
        if (n[0] == '.' && (!n[1] || (n[1] == '.' && !n[2]))) continue;
        int isdir;
        if (e->d_type == DT_DIR) isdir = 1;
        else if (e->d_type == DT_UNKNOWN) {
            struct stat st;
            isdir = fstatat(fd, n, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(st.st_mode);
        } else isdir = 0;
        if (!isdir || pruned(n)) continue;
        size_t nlen = strlen(n), len = plen + 1 + nlen;
        char *child = malloc(len + 1);
        memcpy(child, path, plen); child[plen] = '/'; memcpy(child + plen + 1, n, nlen + 1);
        emit(o, child, len);
        if (nk == ck) { ck = ck ? ck * 2 : 32; kids = realloc(kids, ck * sizeof *kids); }
        kids[nk++] = child;
    }
    closedir(d);
    if (nk) {
        pthread_mutex_lock(&mu);
        for (size_t i = 0; i < nk; i++) push_locked(kids[i]);
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&mu);
    }
    free(kids);
}

static void *worker(void *arg) {
    (void)arg;
    outbuf *o = malloc(sizeof *o); o->n = 0;
    pthread_mutex_lock(&mu);
    for (;;) {
        while (work.len == 0 && busy > 0) pthread_cond_wait(&cv, &mu);
        if (work.len == 0) break;  // queue empty and nobody can add more: done
        char *path = work.items[--work.len];
        busy++;
        pthread_mutex_unlock(&mu);
        scan_dir(path, o);
        free(path);
        pthread_mutex_lock(&mu);
        if (--busy == 0 && work.len == 0) pthread_cond_broadcast(&cv);
    }
    pthread_mutex_unlock(&mu);
    flush(o); free(o);
    return NULL;
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : getenv("HOME");
    if (!root) { fprintf(stderr, "usage: cdf-scan [ROOT]\n"); return 2; }
    char *r = strdup(root);
    size_t rl = strlen(r);
    while (rl > 1 && r[rl - 1] == '/') r[--rl] = 0;
    push_locked(r);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    const char *env = getenv("CDF_SCAN_THREADS");
    if (env) n = atol(env);
    if (n < 1) n = 1;
    if (!env && n > 16) n = 16;  // no measurable gain past ~16 threads
    if (n > 64) n = 64;
    pthread_t t[64];
    for (long i = 0; i < n; i++) pthread_create(&t[i], NULL, worker, NULL);
    for (long i = 0; i < n; i++) pthread_join(t[i], NULL);
    return 0;
}
