# fastjump

Fuzzy `cd` to any directory under `$HOME`, using [fzf](https://github.com/junegunn/fzf).

`fastjump` (alias `fj`) opens instantly from a cached directory list and refreshes the cache in
the background after every use. Directories you pick often and recently are
listed first ("frecency", like zoxide). The cache is built by `fastjump-scan`, a
small multithreaded C scanner.

## Install

Needs bash, `git`, `make`, a C compiler, and **fzf 0.48 or newer** (for
`fzf --bash`). Tested on Kali Linux under WSL2; the other distro commands below
use each distro's standard package names.

**1. Install the dependencies**

| Distro | Command |
|---|---|
| Debian / Ubuntu / Kali / Mint / Pop!_OS | `sudo apt install git make gcc fzf` |
| Fedora / RHEL / Rocky / Alma | `sudo dnf install git make gcc fzf` |
| Arch / Manjaro / EndeavourOS | `sudo pacman -S --needed git make gcc fzf` |
| openSUSE | `sudo zypper install git make gcc fzf` |
| Alpine | `sudo apk add bash git make gcc musl-dev coreutils fzf` |
| Void | `sudo xbps-install -S git make gcc fzf` |
| Gentoo | `sudo emerge --ask dev-vcs/git app-shells/fzf` (make and gcc are already present) |
| NixOS / Nix | `nix-shell -p git gnumake gcc fzf` (or add them to your config) |

On RHEL, Rocky and Alma, fzf comes from EPEL: run `sudo dnf install epel-release`
first. On Alpine, `coreutils` is needed because BusyBox's `realpath` lacks `-m`.

Then check the fzf version:

```bash
fzf --version                # must be 0.48 or newer
```

**If your fzf is older than 0.48** (common on Ubuntu 22.04/24.04, Debian 12 and
other older LTS releases), install the current release from fzf's own repo and
use it instead of the distro package:

```bash
sudo apt remove fzf           # or your distro's equivalent, so the old one isn't picked up
git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
~/.fzf/install --bin          # downloads the binary into ~/.fzf/bin
echo 'export PATH="$HOME/.fzf/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc && fzf --version
```

Put that `PATH` line **above** the `fastjump.bash` line you add in step 4.

**2. Clone the repo**

```bash
git clone https://github.com/abyts1/fastjump.git ~/tools/fastjump
cd ~/tools/fastjump
```

Any location works. If you clone somewhere else, use that path in step 4.

**3. Build and install the scanner**

```bash
make install                 # installs fastjump-scan to ~/.local/bin
command -v fastjump-scan          # should print ~/.local/bin/fastjump-scan
```

If `command -v` prints nothing, `~/.local/bin` isn't on your `PATH`. Add
`export PATH="$HOME/.local/bin:$PATH"` to `~/.bashrc`, or install somewhere
that is: `make install PREFIX=/usr/local` (with `sudo`).

**4. Load it from your shell**

```bash
echo '[ -f ~/tools/fastjump/fastjump.bash ] && . ~/tools/fastjump/fastjump.bash' >> ~/.bashrc
source ~/.bashrc
```

If you already load fzf's key bindings elsewhere in `~/.bashrc` (for example
`eval "$(fzf --bash)"`), remove that line. `fastjump.bash` does it for you.

**zsh:** load `fastjump.zsh` instead (bash must still be installed, since it runs
the functions in `fastjump.bash`):

```zsh
echo '[ -f ~/tools/fastjump/fastjump.zsh ] && . ~/tools/fastjump/fastjump.zsh' >> ~/.zshrc
source ~/.zshrc
```

`fastjump.zsh` only defines `fastjump` and `fj`. It doesn't load fzf's key bindings, so keep (or
add) `source <(fzf --zsh)` in `~/.zshrc`. `Alt-C` then uses fzf's own listing,
not the fastjump cache.

**5. Check it works**

```bash
fastjump          # the first run builds the cache, then opens the picker
```

Type part of a folder name and press Enter to jump there; press Esc to cancel.

**6. (Optional) Bring over your autojump history**

```bash
fastjump --import-autojump
```

This seeds the ranking with the directories autojump already knows, so `fastjump`
puts your usual places first from day one. Running it again only adds new ones.

### Updating

```bash
cd ~/tools/fastjump && git pull && make install
```

Then open a new terminal.

### Uninstalling

```bash
rm -f ~/.local/bin/fastjump-scan ~/.cache/fastjump_dirs
rm -rf ~/.local/share/fastjump          # your frecency history
# then delete the fastjump.bash line from ~/.bashrc (or fastjump.zsh from ~/.zshrc), and optionally ~/tools/fastjump
```

## Usage

| Command | What it does |
|---|---|
| `fastjump` or `fj` | Pick any directory under `~` and cd into it |
| `fastjump DIR` | Only show directories under `DIR` |
| `fastjump -r` | Rescan before opening (e.g. right after creating directories) |
| `fastjump --import-autojump` | Seed the ranking from autojump's history |
| `Alt-C` | Pick a directory under the current one (uses the same cache) |

`fastjump.bash` also enables fzf's `Ctrl-R` (history) and `Ctrl-T` (files). In zsh,
`Alt-C` and those keys come from fzf's own zsh bindings instead.

## Ranking

Every directory you jump to with `fastjump` is recorded in
`~/.local/share/fastjump/db` (or `$XDG_DATA_HOME/fastjump/db`) as
`path<TAB>count<TAB>last-visit`. The list fzf sees is:

1. Recorded directories that still exist, best score first. Score is the visit
   count times a recency weight: x4 within an hour, x2 within a day, x0.5
   within a week, x0.25 after that.
2. Every other directory, shortest path first.

fzf runs with `--tiebreak=index`, so among equally good matches the one higher
in that list wins. A strong fuzzy match still beats a weak one. When the
counts add up to more than 10000, all of them are scaled by 0.9 and entries
below 1 are dropped, so directories you stop using fade out.

Only picks made through `fastjump` are recorded; plain `cd` and `Alt-C` are not.

## fastjump-scan

```
fastjump-scan [ROOT]     # default ROOT is $HOME
```

Prints every directory under `ROOT` (not `ROOT` itself), one per line, in no
particular order. Symlinks are not followed and unreadable directories are
skipped silently. Directories whose name contains a tab or newline are also
skipped (with their subtrees), since the tab-delimited, one-per-line cache and
ranking DB can't represent them. These names are pruned: `.git node_modules .cache .wine
__pycache__ .npm .cargo .rustup .local venv .venv` (keep in sync with
`_FZF_PRUNE` in `fastjump.bash`).

Threads default to the CPU count, capped at 10; set `FASTJUMP_SCAN_THREADS` to
override.

Each directory is opened with `openat()` relative to its parent's open file
descriptor, so the kernel doesn't re-resolve the full path for every
directory, and entries are read with raw `getdents64`. Output is identical to
`find` with the same prunes.

Measured on ~60k directories (32-core WSL2, warm cache, median of 15 runs):

| | Time |
|---|---|
| `find` | ~175 ms |
| `fastjump-scan` v1 (full-path `open`, best at 16 threads) | 49.5 ms |
| `fastjump-scan` v2 (`openat`, 10 threads) | 40.5 ms |

Past ~10 threads it gets slower and noisier as threads contend inside the
kernel. Results on other machines and filesystems will differ.

## Troubleshooting

- **A folder I just created doesn't show up.** The list refreshes in the
  background after each use. Run `fastjump -r` to rescan immediately.
- **`fastjump: command not found`.** The `fastjump.bash` line isn't in `~/.bashrc` (or
  `fastjump.zsh` in `~/.zshrc`), or you haven't opened a new terminal since adding it.
- **`Alt-C` does nothing (Windows Terminal).** The terminal is catching Alt.
  Press `Esc` then `c` instead.
- **Without `fastjump-scan`** on `PATH`, `fastjump` still works but builds its list with
  `find`, which is slower.
