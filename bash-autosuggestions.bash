# bash-autosuggestions loader.
#
# Source this file from an interactive bash startup file after building:
#
#   source /path/to/bash-autosuggestions.bash

case $- in
  *i*) ;;
  *) return 0 2>/dev/null || exit 0 ;;
esac

_bash_autosuggestions_dir=${BASH_SOURCE[0]}
_bash_autosuggestions_dir=${_bash_autosuggestions_dir%/*}
if [[ $_bash_autosuggestions_dir == "$BASH_SOURCE" ]]; then
  _bash_autosuggestions_dir=.
fi

if ! enable -f "$_bash_autosuggestions_dir/bash-autosuggestions.so" bash_autosuggestions 2>/dev/null; then
  printf 'bash-autosuggestions: failed to load %s\n' "$_bash_autosuggestions_dir/bash-autosuggestions.so" >&2
  unset _bash_autosuggestions_dir
  return 1 2>/dev/null || exit 1
fi

bash_autosuggestions enable
unset _bash_autosuggestions_dir
