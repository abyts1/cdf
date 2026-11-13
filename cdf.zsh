# cdf for zsh. Source from ~/.zshrc (adjust the path to your clone):
#   [ -f ~/tools/cdf/cdf.zsh ] && . ~/tools/cdf/cdf.zsh
# The scanning, listing and frecency logic lives in cdf.bash; this file runs
# those functions through bash and does the cd in zsh. Unlike cdf.bash it does
# not load fzf's key bindings: use `source <(fzf --zsh)` for those.
command -v fzf >/dev/null || return 0

_CDF_SRC=${0:A:h}/cdf.bash
_CDF_CACHE=~/.cache/cdf_dirs

# Run a cdf.bash function in bash: _cdf_bash FUNC ARGS...
_cdf_bash() { bash -c '. "$0" >/dev/null 2>&1; "$@"' "$_CDF_SRC" "$@"; }

# Same usage as the bash version: cdf [-r] [DIR] | cdf --import-autojump
cdf() {
  [[ -f $_CDF_SRC ]] || { print -u2 "cdf: $_CDF_SRC not found"; return 1; }
  local d
  case $1 in
    --import-autojump) _cdf_bash _cdf_import_autojump; return ;;
    -r) shift; _cdf_bash _cdf_scan ;;
    *) [[ -s $_CDF_CACHE ]] || _cdf_bash _cdf_scan ;;
  esac
  d=$(CDF_ROOT=${1:-$HOME} _cdf_bash eval '_cdf_list "$(realpath -m "$CDF_ROOT")" |
        fzf --height 40% --layout=reverse --border --scheme=path --tiebreak=index \
            --preview "ls -A --color=always {}"') &&
    cd "$d" && { _cdf_bash _cdf_add "$d" &! } 2>/dev/null
  { _cdf_bash _cdf_scan &! } 2>/dev/null
}
