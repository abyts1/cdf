# fastjump for zsh. Source from ~/.zshrc (adjust the path to your clone):
#   [ -f ~/tools/fastjump/fastjump.zsh ] && . ~/tools/fastjump/fastjump.zsh
# The scanning, listing and frecency logic lives in fastjump.bash; this file runs
# those functions through bash and does the cd in zsh. Unlike fastjump.bash it does
# not load fzf's key bindings: use `source <(fzf --zsh)` for those.
command -v fzf >/dev/null || return 0

_FJ_SRC=${0:A:h}/fastjump.bash
_FJ_CACHE=~/.cache/fastjump_dirs

# Run a fastjump.bash function in bash: _fj_bash FUNC ARGS...
_fj_bash() { bash -c '. "$0" >/dev/null 2>&1; "$@"' "$_FJ_SRC" "$@"; }

# Same usage as the bash version: fastjump [-r] [DIR] | fastjump --import-autojump
fastjump() {
  [[ -f $_FJ_SRC ]] || { print -u2 "fastjump: $_FJ_SRC not found"; return 1; }
  local d
  case $1 in
    --import-autojump) _fj_bash _fj_import_autojump; return ;;
    -r) shift; _fj_bash _fj_scan ;;
    *) [[ -s $_FJ_CACHE ]] || _fj_bash _fj_scan ;;
  esac
  d=$(FJ_ROOT=${1:-$HOME} _fj_bash eval '_fj_list "$(realpath -m "$FJ_ROOT")" |
        fzf --height 40% --layout=reverse --border --scheme=path --tiebreak=index \
            --preview "ls -A --color=always {}"') &&
    cd "$d" && { _fj_bash _fj_add "$d" &! } 2>/dev/null
  { _fj_bash _fj_scan &! } 2>/dev/null
}

alias fj=fastjump
