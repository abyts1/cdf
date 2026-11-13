# fastjump + fzf shell integration. Source from ~/.bashrc (adjust the path to your clone):
#   [ -f ~/tools/fastjump/fastjump.bash ] && . ~/tools/fastjump/fastjump.bash
command -v fzf >/dev/null || return 0

eval "$(fzf --bash)"   # Ctrl-T, Ctrl-R, Alt-C, and ** completion

# Folder names skipped by the find fallback. Keep in sync with PRUNE in fastjump-scan.c.
_FZF_PRUNE='\( -name .git -o -name node_modules -o -name .cache -o -name .wine -o -name __pycache__ -o -name .npm -o -name .cargo -o -name .rustup -o -name .local -o -name venv -o -name .venv \) -prune'
_FJ_CACHE=~/.cache/fastjump_dirs

export FZF_DEFAULT_OPTS='--height 40% --layout=reverse --border'
export FZF_CTRL_T_COMMAND="find . $_FZF_PRUNE -o -type f -print 2>/dev/null | sed 's|^\./||'"
export FZF_CTRL_T_OPTS="--preview 'head -100 {}'"
export FZF_ALT_C_OPTS="--preview 'ls -A --color=always {} | head -50'"

# Rebuild the directory cache atomically. Uses fastjump-scan (parallel, ~4x faster than find)
# when installed, otherwise find. The list is sorted shortest-path-first so
# that fzf's --tiebreak=index prefers shallower directories among equal matches.
_fj_scan() {
  local tmp="$_FJ_CACHE.tmp.$$"
  mkdir -p "${_FJ_CACHE%/*}"
  {
    if command -v fastjump-scan >/dev/null; then
      fastjump-scan "$HOME"
    else
      eval "find \"$HOME\" -mindepth 1 $_FZF_PRUNE -o -type d -print"
    fi
  } 2>/dev/null | LC_ALL=C awk '{ print length($0) "\t" $0 }' | LC_ALL=C sort -n -k1,1 | cut -f2- > "$tmp"
  mv -f "$tmp" "$_FJ_CACHE"
}

# Alt-C: dirs under $PWD from the cache; live find outside $HOME, inside a
# pruned dir, or when the cache is missing.
_fzf_altc_list() {
  local p="$PWD/" out=""
  if [ -s "$_FJ_CACHE" ] && [[ "$p" == "$HOME"/* ]]; then
    out=$(awk -v p="$p" 'index($0,p)==1{print substr($0,length(p)+1)}' "$_FJ_CACHE")
  fi
  if [ -n "$out" ]; then printf '%s\n' "$out"; ( _fj_scan & ) >/dev/null 2>&1
  else eval "find . -mindepth 1 $_FZF_PRUNE -o -type d -print" 2>/dev/null | sed 's|^\./||'; fi
}
export _FZF_PRUNE _FJ_CACHE; export -f _fzf_altc_list _fj_scan
export FZF_ALT_C_COMMAND='_fzf_altc_list'

# --- frecency ---------------------------------------------------------------
# Every directory you pick with fastjump is recorded as "path<TAB>count<TAB>last-epoch".
# Rank = count weighted by recency (x4 within an hour, x2 within a day, x0.5
# within a week, x0.25 older), the same scheme zoxide uses.
_FJ_DB=${XDG_DATA_HOME:-$HOME/.local/share}/fastjump/db

# Record a visit. When the counts add up to more than 10000, every count is
# scaled by 0.9 and entries that drop below 1 are forgotten, so old habits fade.
_fj_add() {
  local tmp="$_FJ_DB.tmp.$$"
  mkdir -p "${_FJ_DB%/*}"
  [ -f "$_FJ_DB" ] || : > "$_FJ_DB"
  awk -F'\t' -v OFS='\t' -v p="$1" -v now="$(date +%s)" '
    $1 == p { hit = NR }
    { path[NR] = $1; cnt[NR] = $2; last[NR] = $3; sum += $2 }
    END {
      f = sum + 1 > 10000 ? 0.9 : 1   # age the old counts first, then add this visit
      for (i = 1; i <= NR; i++) {
        c = cnt[i] * f
        if (i == hit) { c += 1; last[i] = now }
        if (c >= 1) print path[i], c, last[i]
      }
      if (!hit) print p, 1, now
    }' "$_FJ_DB" 2>/dev/null > "$tmp" && mv -f "$tmp" "$_FJ_DB"
}

# Print directories under $1: frecent ones first (best first, only those that
# still exist), then everything else from the cache.
_fj_list() {
  local root="$1/" now d
  printf -v now '%(%s)T' -1
  if [ -s "$_FJ_DB" ]; then
    awk -F'\t' -v r="$root" -v now="$now" 'index($1, r) == 1 {
        a = now - $3
        w = a < 3600 ? 4 : a < 86400 ? 2 : a < 604800 ? 0.5 : 0.25
        print $2 * w "\t" $1 }' "$_FJ_DB" | sort -t $'\t' -k1,1gr | cut -f2- |
      while IFS= read -r d; do [ -d "$d" ] && printf '%s\n' "$d"; done
  fi
  awk -v r="$root" -v db="$_FJ_DB" '
    BEGIN { while ((getline l < db) > 0) { split(l, f, "\t"); seen[f[1]] } }
    index($0, r) == 1 && !($0 in seen)' "$_FJ_CACHE"
}

# Seed the frecency DB from autojump (weights become counts, dated a week ago).
_fj_import_autojump() {
  local src=${XDG_DATA_HOME:-$HOME/.local/share}/autojump/autojump.txt
  [ -s "$src" ] || { echo "fastjump: no autojump database at $src" >&2; return 1; }
  mkdir -p "${_FJ_DB%/*}"
  local then=$(( $(date +%s) - 604800 )) tmp="$_FJ_DB.tmp.$$"
  awk -F'\t' -v OFS='\t' -v t="$then" -v db="$_FJ_DB" '
    BEGIN { while ((getline l < db) > 0) { split(l, f, "\t"); have[f[1]] = l } }
    !($2 in have) { c = int($1 / 10); have[$2] = $2 OFS (c < 1 ? 1 : c) OFS t; n++ }
    END { for (k in have) print have[k]; print n + 0 > "/dev/stderr" }' "$src" 2>"$tmp.n" > "$tmp"
  mv -f "$tmp" "$_FJ_DB"
  echo "fastjump: imported $(cat "$tmp.n") directories from autojump"; rm -f "$tmp.n"
}

# fastjump [-r] [DIR]: fuzzy-pick a directory under DIR (default $HOME) and cd to it.
# Directories you pick often and recently are listed first. Reads the cache
# instantly and refreshes it in the background; -r rescans first.
# fastjump --import-autojump: seed the ranking from your autojump history.
fastjump() {
  local d root
  case "$1" in
    --import-autojump) _fj_import_autojump; return ;;
    -r) shift; _fj_scan ;;
    *) [ -s "$_FJ_CACHE" ] || _fj_scan ;;
  esac
  root=$(realpath -m "${1:-$HOME}")
  d=$(_fj_list "$root" | fzf --scheme=path --tiebreak=index --preview 'ls -A --color=always {}') &&
    cd "$d" && ( _fj_add "$d" & ) 2>/dev/null
  ( _fj_scan & ) 2>/dev/null
}

alias fj=fastjump
