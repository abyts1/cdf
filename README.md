# cdf

Fuzzy `cd` to any directory under `$HOME`, using [fzf](https://github.com/junegunn/fzf).

`cdf` opens instantly from a cached directory list and refreshes the cache in
the background after every use. The cache is built by `cdf-scan`, a small
multithreaded C scanner that walks the tree in parallel.

## Install

```bash
make install                 # builds cdf-scan into ~/.local/bin (PREFIX=... to change)
echo '[ -f ~/tools/cdf/cdf.bash ] && . ~/tools/cdf/cdf.bash' >> ~/.bashrc
```

Requires `fzf` (0.48+) and a C compiler. Without `cdf-scan` on `PATH`,
`cdf.bash` falls back to `find`.

## Usage

| Command | What it does |
|---|---|
| `cdf` | Pick any directory under `~` and cd into it |
| `cdf DIR` | Only show directories under `DIR` |
| `cdf -r` | Rescan before opening (e.g. right after creating directories) |
| `Alt-C` | Pick a directory under the current one (uses the same cache) |

`cdf.bash` also enables fzf's `Ctrl-R` (history) and `Ctrl-T` (files).

## cdf-scan

```
cdf-scan [ROOT]     # default ROOT is $HOME
```

Prints every directory under `ROOT` (not `ROOT` itself), one per line, in no
particular order. Symlinks are not followed and unreadable directories are
skipped silently. These names are pruned: `.git node_modules .cache .wine
__pycache__ .npm .cargo .rustup .local venv .venv` (keep in sync with
`_FZF_PRUNE` in `cdf.bash`).

Threads default to the CPU count, capped at 16; set `CDF_SCAN_THREADS` to
override.

Measured on ~60k directories (WSL2, warm cache, best of 5): `find` 0.188 s,
`cdf-scan` 0.049 s at 16 threads. Single-threaded it is slower than `find`
(0.384 s); the gain comes from parallelism.
