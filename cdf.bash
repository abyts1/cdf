# cdf + fzf shell integration. Source from ~/.bashrc:
#   [ -f ~/tools/cdf/cdf.bash ] && . ~/tools/cdf/cdf.bash
command -v fzf >/dev/null || return 0

eval "$(fzf --bash)"   # Ctrl-T, Ctrl-R, Alt-C, and ** completion

# Folder names skipped by the find fallback. Keep in sync with PRUNE in cdf-scan.c.
_FZF_PRUNE='\( -name .git -o -name node_modules -o -name .cache -o -name .wine -o -name __pycache__ -o -name .npm -o -name .cargo -o -name .rustup -o -name .local -o -name venv -o -name .venv \) -prune'
_CDF_CACHE=~/.cache/cdf_dirs

export FZF_DEFAULT_OPTS='--height 40% --layout=reverse --border'
export FZF_CTRL_T_COMMAND="find . $_FZF_PRUNE -o -type f -print 2>/dev/null | sed 's|^\./||'"
export FZF_CTRL_T_OPTS="--preview 'head -100 {}'"
export FZF_ALT_C_OPTS="--preview 'ls -A --color=always {} | head -50'"

# Rebuild the directory cache atomically. Uses cdf-scan (parallel, ~4x faster)
# when installed, otherwise find.
_cdf_scan() {
  local tmp="$_CDF_CACHE.tmp.$$"
  mkdir -p "${_CDF_CACHE%/*}"
  if command -v cdf-scan >/dev/null; then
    cdf-scan "$HOME" > "$tmp" 2>/dev/null
  else
    eval "find \"$HOME\" -mindepth 1 $_FZF_PRUNE -o -type d -print" > "$tmp" 2>/dev/null
  fi
  mv -f "$tmp" "$_CDF_CACHE"
}

# Alt-C: dirs under $PWD from the cache; live find outside $HOME, inside a
# pruned dir, or when the cache is missing.
_fzf_altc_list() {
  local p="$PWD/" out=""
  if [ -s "$_CDF_CACHE" ] && [[ "$p" == "$HOME"/* ]]; then
    out=$(awk -v p="$p" 'index($0,p)==1{print substr($0,length(p)+1)}' "$_CDF_CACHE")
  fi
  if [ -n "$out" ]; then printf '%s\n' "$out"; ( _cdf_scan & ) >/dev/null 2>&1
  else eval "find . -mindepth 1 $_FZF_PRUNE -o -type d -print" 2>/dev/null | sed 's|^\./||'; fi
}
export _FZF_PRUNE _CDF_CACHE; export -f _fzf_altc_list _cdf_scan
export FZF_ALT_C_COMMAND='_fzf_altc_list'

# cdf [-r] [DIR]: fuzzy-pick a directory under DIR (default $HOME) and cd to it.
# Reads the cache instantly and refreshes it in the background; -r rescans first.
cdf() {
  local d root
  if [ "$1" = "-r" ]; then shift; _cdf_scan
  elif [ ! -s "$_CDF_CACHE" ]; then _cdf_scan; fi
  root=$(realpath -m "${1:-$HOME}")
  d=$(grep -F -- "$root/" "$_CDF_CACHE" | fzf --scheme=path --preview 'ls -A --color=always {}') && cd "$d"
  ( _cdf_scan & ) 2>/dev/null
}
