# cdf

Fuzzy `cd` to any directory under `$HOME`, using [fzf](https://github.com/junegunn/fzf).

`cdf` opens instantly from a cached directory list and refreshes the cache in
the background after every use. The cache is built by `cdf-scan`, a small
multithreaded C scanner that walks the tree in parallel.

## Install

Tested on Linux (Debian/Kali, WSL2) with bash. Needs `git`, `make`, a C
compiler, and **fzf 0.48 or newer** (for `fzf --bash`).

**1. Install the dependencies**

```bash
sudo apt install git make gcc fzf     # Debian / Ubuntu / Kali
fzf --version                         # must be 0.48+
```

If your distro's fzf is older than 0.48, install it from
[fzf's releases](https://github.com/junegunn/fzf/releases) instead.

**2. Clone the repo**

```bash
git clone https://github.com/abyts1/cdf.git ~/tools/cdf
cd ~/tools/cdf
```

Any location works. If you clone somewhere else, use that path in step 4.

**3. Build and install the scanner**

```bash
make install                 # installs cdf-scan to ~/.local/bin
command -v cdf-scan          # should print ~/.local/bin/cdf-scan
```

If `command -v` prints nothing, `~/.local/bin` isn't on your `PATH`. Add
`export PATH="$HOME/.local/bin:$PATH"` to `~/.bashrc`, or install somewhere
that is: `make install PREFIX=/usr/local` (with `sudo`).

**4. Load it from your shell**

```bash
echo '[ -f ~/tools/cdf/cdf.bash ] && . ~/tools/cdf/cdf.bash' >> ~/.bashrc
source ~/.bashrc
```

If you already load fzf's key bindings elsewhere in `~/.bashrc` (for example
`eval "$(fzf --bash)"`), remove that line. `cdf.bash` does it for you.

**5. Check it works**

```bash
cdf          # the first run builds the cache, then opens the picker
```

Type part of a folder name and press Enter to jump there; press Esc to cancel.

### Updating

```bash
cd ~/tools/cdf && git pull && make install
```

Then open a new terminal.

### Uninstalling

```bash
rm -f ~/.local/bin/cdf-scan ~/.cache/cdf_dirs
# then delete the cdf.bash line from ~/.bashrc, and optionally ~/tools/cdf
```

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

## Troubleshooting

- **A folder I just created doesn't show up.** The list refreshes in the
  background after each use. Run `cdf -r` to rescan immediately.
- **`cdf: command not found`.** The `cdf.bash` line isn't in `~/.bashrc`, or
  you haven't opened a new terminal since adding it.
- **`Alt-C` does nothing (Windows Terminal).** The terminal is catching Alt.
  Press `Esc` then `c` instead.
- **Without `cdf-scan`** on `PATH`, `cdf` still works but builds its list with
  `find`, which is slower.
