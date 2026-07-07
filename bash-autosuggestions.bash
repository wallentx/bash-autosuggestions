# bash-autosuggestions loader.
#
# Source this file from an interactive bash startup file after building:
#
#   source /path/to/bash-autosuggestions.bash

case $- in
  *i*) ;;
  *) return 0 2>/dev/null || exit 0 ;;
esac

_bash_autosuggestions_source=${BASH_SOURCE[0]}
_bash_autosuggestions_dir=${_bash_autosuggestions_source%/*}
if [[ $_bash_autosuggestions_dir == "$_bash_autosuggestions_source" ]]; then
  _bash_autosuggestions_dir=.
fi
if [[ $_bash_autosuggestions_dir != /* ]]; then
  _bash_autosuggestions_dir=$(
    cd -P -- "$_bash_autosuggestions_dir" >/dev/null 2>&1 && pwd
  )
fi
_BASH_AUTOSUGGESTIONS_LOADER=$_bash_autosuggestions_dir/bash-autosuggestions.bash

if ! enable -f "$_bash_autosuggestions_dir/bash-autosuggestions.so" bash_autosuggestions 2>/dev/null; then
  printf 'bash-autosuggestions: failed to load %s\n' "$_bash_autosuggestions_dir/bash-autosuggestions.so" >&2
  unset _bash_autosuggestions_dir _bash_autosuggestions_source
  return 1 2>/dev/null || exit 1
fi

_bash_autosuggestions_quote() {
  printf '%q' "$1"
}

_bash_autosuggestions_strategy_tokens() {
  local strategy=${1//,/ } token
  local tokens=()
  _bas_strategy_tokens=()
  read -r -a tokens <<<"$strategy"
  for token in "${tokens[@]}"; do
    [[ -n $token ]] && _bas_strategy_tokens+=("$token")
  done
}

_bash_autosuggestions_strategy_assignment() {
  local strategy=$1 token sep=
  _bash_autosuggestions_strategy_tokens "$strategy"
  if ((${#_bas_strategy_tokens[@]} == 0)); then
    printf 'unset BASH_AUTOSUGGEST_STRATEGY\n'
    return
  fi

  printf 'BASH_AUTOSUGGEST_STRATEGY=('
  for token in "${_bas_strategy_tokens[@]}"; do
    printf '%s%s' "$sep" "$(_bash_autosuggestions_quote "$token")"
    sep=' '
  done
  printf ')\n'
}

_bash_autosuggestions_apply_strategy_current() {
  _bash_autosuggestions_strategy_tokens "$_bas_cfg_strategy"
  if ((${#_bas_strategy_tokens[@]} == 0)); then
    unset BASH_AUTOSUGGEST_STRATEGY
  else
    BASH_AUTOSUGGEST_STRATEGY=("${_bas_strategy_tokens[@]}")
  fi
}

_bash_autosuggestions_setup_colors() {
  if [[ -t 1 && -z ${NO_COLOR+x} && ${TERM:-} != dumb ]]; then
    _bas_c_reset=$'\033[0m'
    _bas_c_title=$'\033[1;38;5;45m'
    _bas_c_heading=$'\033[1;38;5;39m'
    _bas_c_label=$'\033[1;38;5;75m'
    _bas_c_accent=$'\033[38;5;45m'
    _bas_c_dim=$'\033[2m'
    _bas_c_ok=$'\033[38;5;82m'
    _bas_c_warn=$'\033[38;5;214m'
    _bas_c_select=$'\033[7m'
    _bas_c_cmd=$'\033[1;38;5;220m'
  else
    _bas_c_reset=
    _bas_c_title=
    _bas_c_heading=
    _bas_c_label=
    _bas_c_accent=
    _bas_c_dim=
    _bas_c_ok=
    _bas_c_warn=
    _bas_c_select=
    _bas_c_cmd=
  fi
}

_bash_autosuggestions_help() {
  _bash_autosuggestions_setup_colors
  printf '\n%s%s%s\n' "$_bas_c_title" 'bash_autosuggestions' "$_bas_c_reset"
  printf '  %sGhost-text command suggestions for interactive Bash.%s\n\n' "$_bas_c_dim" "$_bas_c_reset"
  printf '%sUsage%s\n' "$_bas_c_heading" "$_bas_c_reset"
  printf '  %sbash_autosuggestions%s [command]\n\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '%sCommands%s\n' "$_bas_c_heading" "$_bas_c_reset"
  printf '  %senable%s       enable autosuggestions in this shell\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %sdisable%s      disable autosuggestions in this shell\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %stoggle%s       toggle autosuggestions on or off\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %sclear%s        clear the visible suggestion\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %sfetch%s        refresh the suggestion for the current line\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %sbind%s         rebind configured Readline keys\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %sstatus%s       show whether the builtin is installed and enabled\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %suninstall%s    remove hooks and key bindings from this shell\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %sconfig%s       open the guided configuration UI\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %sconfigure%s    alias for config\n\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '%sOptions%s\n' "$_bas_c_heading" "$_bas_c_reset"
  printf '  %s-h, --help%s   show this help\n\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '%sExamples%s\n' "$_bas_c_heading" "$_bas_c_reset"
  printf '  bash_autosuggestions config\n'
  printf '  bash_autosuggestions config --profile smart --print\n'
  printf '  bash_autosuggestions status\n'
}

_bash_autosuggestions_setup_colors

_bash_autosuggestions_profile() {
  local profile=${1:-smart}
  case "$profile" in
    minimal|default)
      printf '%s\n' \
        'async=auto' \
        'strategy=history' \
        'style=fg=8' \
        'buffer_max=' \
        'history_ignore=' \
        'completion_ignore=' \
        'accept_keyseqs=' \
        'execute_keyseqs=' \
        'clear_keyseqs=' \
        'partial_keyseqs='
      ;;
    smart)
      printf '%s\n' \
        'async=auto' \
        'strategy=match_prev_cmd completion' \
        'style=fg=8' \
        'buffer_max=' \
        'history_ignore=' \
        'completion_ignore=' \
        'accept_keyseqs=' \
        'execute_keyseqs=' \
        'clear_keyseqs=' \
        'partial_keyseqs=\ef'
      ;;
    fast)
      printf '%s\n' \
        'async=1' \
        'strategy=history' \
        'style=fg=8' \
        'buffer_max=' \
        'history_ignore=' \
        'completion_ignore=' \
        'accept_keyseqs=' \
        'execute_keyseqs=' \
        'clear_keyseqs=' \
        'partial_keyseqs='
      ;;
    safe)
      printf '%s\n' \
        'async=0' \
        'strategy=history' \
        'style=fg=8' \
        'buffer_max=120' \
        'history_ignore=*password*' \
        'completion_ignore=' \
        'accept_keyseqs=' \
        'execute_keyseqs=' \
        'clear_keyseqs=' \
        'partial_keyseqs='
      ;;
    completion)
      printf '%s\n' \
        'async=auto' \
        'strategy=match_prev_cmd history completion' \
        'style=fg=244' \
        'buffer_max=' \
        'history_ignore=' \
        'completion_ignore=' \
        'accept_keyseqs=' \
        'execute_keyseqs=' \
        'clear_keyseqs=' \
        'partial_keyseqs=\ef'
      ;;
    *)
      return 1
      ;;
  esac
}

_bash_autosuggestions_apply_profile() {
  local line key value profile_data
  profile_data=$(_bash_autosuggestions_profile "$1") || return 1
  while IFS= read -r line; do
    key=${line%%=*}
    value=${line#*=}
    case "$key" in
      async) _bas_cfg_async=$value ;;
      strategy) _bas_cfg_strategy=$value ;;
      style) _bas_cfg_style=$value ;;
      buffer_max) _bas_cfg_buffer_max=$value ;;
      history_ignore) _bas_cfg_history_ignore=$value ;;
      completion_ignore) _bas_cfg_completion_ignore=$value ;;
      accept_keyseqs) _bas_cfg_accept_keyseqs=$value ;;
      execute_keyseqs) _bas_cfg_execute_keyseqs=$value ;;
      clear_keyseqs) _bas_cfg_clear_keyseqs=$value ;;
      partial_keyseqs) _bas_cfg_partial_keyseqs=$value ;;
    esac
  done <<<"$profile_data"
}

_bash_autosuggestions_color_index() {
  case "$1" in
    black) printf '0' ;;
    red) printf '1' ;;
    green) printf '2' ;;
    yellow) printf '3' ;;
    blue) printf '4' ;;
    magenta) printf '5' ;;
    cyan) printf '6' ;;
    white) printf '7' ;;
    grey|gray) printf '8' ;;
    *) return 1 ;;
  esac
}

_bash_autosuggestions_style_sequence() {
  local style=${1:-fg=8}
  local params= part token key value idx r g b
  local old_ifs=$IFS
  IFS=,
  for token in $style; do
    IFS=$old_ifs
    token=${token#"${token%%[![:space:]]*}"}
    token=${token%"${token##*[![:space:]]}"}
    part=
    case "$token" in
      fg=*)
        value=${token#fg=}
        if [[ $value =~ ^#[0-9A-Fa-f]{6}$ ]]; then
          r=$((16#${value:1:2}))
          g=$((16#${value:3:2}))
          b=$((16#${value:5:2}))
          part="38;2;$r;$g;$b"
        elif [[ $value =~ ^[0-9]+$ ]]; then
          part="38;5;$value"
        elif idx=$(_bash_autosuggestions_color_index "$value"); then
          part="38;5;$idx"
        fi
        ;;
      bg=*)
        value=${token#bg=}
        if [[ $value =~ ^#[0-9A-Fa-f]{6}$ ]]; then
          r=$((16#${value:1:2}))
          g=$((16#${value:3:2}))
          b=$((16#${value:5:2}))
          part="48;2;$r;$g;$b"
        elif [[ $value =~ ^[0-9]+$ ]]; then
          part="48;5;$value"
        elif idx=$(_bash_autosuggestions_color_index "$value"); then
          part="48;5;$idx"
        fi
        ;;
      bold) part=1 ;;
      underline) part=4 ;;
      standout) part=7 ;;
    esac
    if [[ -n $part ]]; then
      if [[ -n $params ]]; then
        params+=\;
      fi
      params+=$part
    fi
    IFS=,
  done
  IFS=$old_ifs
  if [[ -z $params ]]; then
    params='38;5;8'
  fi
  printf '\033[%sm' "$params"
}

_bash_autosuggestions_preview_style() {
  local style=${1:-fg=8}
  local seq reset=$'\033[0m'
  seq=$(_bash_autosuggestions_style_sequence "$style")
  printf '\nHighlight preview (%s)\n' "$style"
  printf '  %s%s%s%s\n\n' 'git ch' "$seq" 'eckout main -- src/bash_autosuggestions.c' "$reset"
}

_bash_autosuggestions_config_block() {
  local loader=${_bas_cfg_loader:-${_BASH_AUTOSUGGESTIONS_LOADER:-bash-autosuggestions.bash}}
  printf '# >>> bash-autosuggestions >>>\n'
  printf 'BASH_AUTOSUGGEST_USE_ASYNC=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_async")"
  _bash_autosuggestions_strategy_assignment "$_bas_cfg_strategy"
  printf 'BASH_AUTOSUGGEST_HIGHLIGHT_STYLE=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_style")"
  if [[ -n $_bas_cfg_buffer_max ]]; then
    printf 'BASH_AUTOSUGGEST_BUFFER_MAX_SIZE=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_buffer_max")"
  else
    printf 'unset BASH_AUTOSUGGEST_BUFFER_MAX_SIZE\n'
  fi
  if [[ -n $_bas_cfg_history_ignore ]]; then
    printf 'BASH_AUTOSUGGEST_HISTORY_IGNORE=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_history_ignore")"
  else
    printf 'unset BASH_AUTOSUGGEST_HISTORY_IGNORE\n'
  fi
  if [[ -n $_bas_cfg_completion_ignore ]]; then
    printf 'BASH_AUTOSUGGEST_COMPLETION_IGNORE=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_completion_ignore")"
  else
    printf 'unset BASH_AUTOSUGGEST_COMPLETION_IGNORE\n'
  fi
  if [[ -n $_bas_cfg_accept_keyseqs ]]; then
    printf 'BASH_AUTOSUGGEST_ACCEPT_KEYSEQS=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_accept_keyseqs")"
  else
    printf 'unset BASH_AUTOSUGGEST_ACCEPT_KEYSEQS\n'
  fi
  if [[ -n $_bas_cfg_execute_keyseqs ]]; then
    printf 'BASH_AUTOSUGGEST_EXECUTE_KEYSEQS=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_execute_keyseqs")"
  else
    printf 'unset BASH_AUTOSUGGEST_EXECUTE_KEYSEQS\n'
  fi
  if [[ -n $_bas_cfg_clear_keyseqs ]]; then
    printf 'BASH_AUTOSUGGEST_CLEAR_KEYSEQS=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_clear_keyseqs")"
  else
    printf 'unset BASH_AUTOSUGGEST_CLEAR_KEYSEQS\n'
  fi
  if [[ -n $_bas_cfg_partial_keyseqs ]]; then
    printf 'BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS=%s\n' "$(_bash_autosuggestions_quote "$_bas_cfg_partial_keyseqs")"
  else
    printf 'unset BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS\n'
  fi
  printf 'source %s\n' "$(_bash_autosuggestions_quote "$loader")"
  printf '# <<< bash-autosuggestions <<<\n'
}

_bash_autosuggestions_write_block() {
  local bashrc=$1 block=$2 tmp
  tmp=$(mktemp "${TMPDIR:-/tmp}/bash-autosuggestions.XXXXXX") || return 1
  if [[ -f $bashrc ]]; then
    awk -v block="$block" '
      BEGIN { in_block = 0; replaced = 0 }
      $0 == "# >>> bash-autosuggestions >>>" {
        if (!replaced) {
          print block
          replaced = 1
        }
        in_block = 1
        next
      }
      $0 == "# <<< bash-autosuggestions <<<" {
        in_block = 0
        next
      }
      !in_block && $0 ~ /^[[:space:]]*(source|\.)[[:space:]].*bash-autosuggestions[.]bash([[:space:]]|$)/ {
        next
      }
      !in_block { print }
      END {
        if (!replaced) {
          if (NR > 0) {
            print ""
          }
          print block
        }
      }
    ' "$bashrc" >"$tmp" || {
      rm -f "$tmp"
      return 1
    }
  else
    printf '%s\n' "$block" >"$tmp" || {
      rm -f "$tmp"
      return 1
    }
  fi
  cat "$tmp" >"$bashrc"
  rm -f "$tmp"
}

_bash_autosuggestions_apply_current() {
  BASH_AUTOSUGGEST_USE_ASYNC=$_bas_cfg_async
  _bash_autosuggestions_apply_strategy_current
  BASH_AUTOSUGGEST_HIGHLIGHT_STYLE=$_bas_cfg_style
  if [[ -n $_bas_cfg_buffer_max ]]; then
    BASH_AUTOSUGGEST_BUFFER_MAX_SIZE=$_bas_cfg_buffer_max
  else
    unset BASH_AUTOSUGGEST_BUFFER_MAX_SIZE
  fi
  if [[ -n $_bas_cfg_history_ignore ]]; then
    BASH_AUTOSUGGEST_HISTORY_IGNORE=$_bas_cfg_history_ignore
  else
    unset BASH_AUTOSUGGEST_HISTORY_IGNORE
  fi
  if [[ -n $_bas_cfg_completion_ignore ]]; then
    BASH_AUTOSUGGEST_COMPLETION_IGNORE=$_bas_cfg_completion_ignore
  else
    unset BASH_AUTOSUGGEST_COMPLETION_IGNORE
  fi
  if [[ -n $_bas_cfg_accept_keyseqs ]]; then
    BASH_AUTOSUGGEST_ACCEPT_KEYSEQS=$_bas_cfg_accept_keyseqs
  else
    unset BASH_AUTOSUGGEST_ACCEPT_KEYSEQS
  fi
  if [[ -n $_bas_cfg_execute_keyseqs ]]; then
    BASH_AUTOSUGGEST_EXECUTE_KEYSEQS=$_bas_cfg_execute_keyseqs
  else
    unset BASH_AUTOSUGGEST_EXECUTE_KEYSEQS
  fi
  if [[ -n $_bas_cfg_clear_keyseqs ]]; then
    BASH_AUTOSUGGEST_CLEAR_KEYSEQS=$_bas_cfg_clear_keyseqs
  else
    unset BASH_AUTOSUGGEST_CLEAR_KEYSEQS
  fi
  if [[ -n $_bas_cfg_partial_keyseqs ]]; then
    BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS=$_bas_cfg_partial_keyseqs
  else
    unset BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS
  fi
  builtin bash_autosuggestions bind
  builtin bash_autosuggestions enable
}

_bash_autosuggestions_menu() {
  local title=$1 default_index=${2:-0} reply key seq idx count i lines
  shift 2
  local options=("$@")

  _bash_autosuggestions_setup_colors
  count=${#options[@]}
  idx=$default_index
  if ((count == 0)); then
    _bas_menu_index=0
    return 0
  fi
  if ((idx < 0 || idx >= count)); then
    idx=0
  fi

  if [[ ! -t 0 || ! -t 1 || ${TERM:-} == dumb ]]; then
    while :; do
      printf '\n%s%s%s\n' "$_bas_c_heading" "$title" "$_bas_c_reset"
      for ((i = 0; i < count; i++)); do
        printf '  %d) %s\n' "$((i + 1))" "${options[$i]}"
      done
      printf 'Choose [%d]: ' "$((idx + 1))"
      IFS= read -r reply || reply=
      [[ -z $reply ]] && break
      if [[ $reply =~ ^[0-9]+$ ]] && ((reply >= 1 && reply <= count)); then
        idx=$((reply - 1))
        break
      fi
      printf 'Please choose a number from 1 to %d.\n' "$count"
    done
    _bas_menu_index=$idx
    return 0
  fi

  lines=$((count + 2))
  printf '\n'
  while :; do
    printf '\033[2K\r%s%s%s\n' "$_bas_c_heading" "$title" "$_bas_c_reset"
    printf '\033[2K\r%sUse Up/Down, j/k, or number keys. Enter selects.%s\n' "$_bas_c_dim" "$_bas_c_reset"
    for ((i = 0; i < count; i++)); do
      if ((i == idx)); then
        printf '\033[2K\r%s> %s%s%s\n' "$_bas_c_accent" "$_bas_c_select" "${options[$i]}" "$_bas_c_reset"
      else
        printf '\033[2K\r  %s\n' "${options[$i]}"
      fi
    done

    IFS= read -rsn1 key || break
    case "$key" in
      '')
        break
        ;;
      $'\r'|$'\n')
        break
        ;;
      $'\033')
        seq=
        IFS= read -rsn2 -t 0.05 seq || true
        case "$seq" in
          '[A') ((idx--)); ((idx < 0)) && idx=$((count - 1)) ;;
          '[B') ((idx++)); ((idx >= count)) && idx=0 ;;
        esac
        ;;
      k|K)
        ((idx--))
        ((idx < 0)) && idx=$((count - 1))
        ;;
      j|J)
        ((idx++))
        ((idx >= count)) && idx=0
        ;;
      [1-9])
        if ((key >= 1 && key <= count)); then
          idx=$((key - 1))
          break
        fi
        ;;
    esac
    printf '\033[%dA' "$lines"
  done
  printf '\n'
  _bas_menu_index=$idx
}

_bash_autosuggestions_select_setting() {
  local title=$1 current=$2 default_index=0 i
  shift 2
  local values=()
  local labels=()

  while (($#)); do
    values+=("$1")
    labels+=("$2")
    shift 2
  done

  for ((i = 0; i < ${#values[@]}; i++)); do
    if [[ ${values[$i]} == "$current" ]]; then
      default_index=$i
      break
    fi
  done

  _bash_autosuggestions_menu "$title" "$default_index" "${labels[@]}"
  _bas_selected_value=${values[$_bas_menu_index]}
}

_bash_autosuggestions_prompt_value() {
  local prompt=$1 current=$2 reply shown
  shown=${current:-unset}
  printf '%s%s%s [%s]: ' "$_bas_c_label" "$prompt" "$_bas_c_reset" "$shown"
  IFS= read -r reply
  if [[ -n $reply ]]; then
    _bas_selected_value=$reply
  else
    _bas_selected_value=$current
  fi
}

_bash_autosuggestions_set_from_menu() {
  local var_name=$1 title=$2 current=$3
  shift 3
  _bash_autosuggestions_select_setting "$title" "$current" "$@"
  if [[ $_bas_selected_value == __custom__ ]]; then
    _bash_autosuggestions_prompt_value "$title" "$current"
  fi
  printf -v "$var_name" '%s' "$_bas_selected_value"
}

_bash_autosuggestions_style_gallery() {
  local style seq reset=$'\033[0m'
  local sample_head='git ch'
  local sample_tail='eckout main -- src/bash_autosuggestions.c'

  printf '\n%sHighlight Style Preview%s\n' "$_bas_c_heading" "$_bas_c_reset"
  for style in 'fg=8' 'fg=244' 'fg=#777777,bold' 'fg=cyan,underline' 'fg=#999999,bg=#202020'; do
    seq=$(_bash_autosuggestions_style_sequence "$style")
    printf '  %-24s %s%s%s%s\n' "$style" "$sample_head" "$seq" "$sample_tail" "$reset"
  done
}

_bash_autosuggestions_config_summary() {
  printf '\n%sSelected Settings%s\n' "$_bas_c_heading" "$_bas_c_reset"
  printf '  %sAsync%s              %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_async:-unset}"
  printf '  %sStrategy%s           %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_strategy:-unset}"
  printf '  %sHighlight%s          %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_style:-unset}"
  printf '  %sBuffer max%s         %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_buffer_max:-unset}"
  printf '  %sHistory ignore%s     %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_history_ignore:-unset}"
  printf '  %sCompletion ignore%s  %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_completion_ignore:-unset}"
  printf '  %sAccept keys%s        %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_accept_keyseqs:-built-in}"
  printf '  %sPartial keys%s       %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_partial_keyseqs:-built-in}"
  printf '  %sExecute keys%s       %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_execute_keyseqs:-built-in}"
  printf '  %sClear keys%s         %s\n' "$_bas_c_label" "$_bas_c_reset" "${_bas_cfg_clear_keyseqs:-built-in}"
}

_bash_autosuggestions_profile_index() {
  case "$1" in
    smart) _bas_profile_index=0 ;;
    minimal|default) _bas_profile_index=1 ;;
    fast) _bas_profile_index=2 ;;
    safe) _bas_profile_index=3 ;;
    completion) _bas_profile_index=4 ;;
    *) _bas_profile_index=0 ;;
  esac
}

_bash_autosuggestions_configure_interactive() {
  local initial_profile=${1:-smart} profile

  _bash_autosuggestions_setup_colors
  _bash_autosuggestions_profile_index "$initial_profile"

  printf '\n%s%s%s\n' "$_bas_c_title" 'Bash Autosuggestions Configurator' "$_bas_c_reset"
  printf '%sChoose a starting profile, then optionally tune every setting.%s\n' "$_bas_c_dim" "$_bas_c_reset"

  _bash_autosuggestions_menu 'Choose a Profile' "$_bas_profile_index" \
    'smart       balanced history context + completion fallback' \
    'minimal     history only, async auto' \
    'fast        history only, async always on' \
    'safe        sync mode, conservative limits and ignore pattern' \
    'completion  match previous command, history, then completion'

  case "$_bas_menu_index" in
    0) profile=smart ;;
    1) profile=minimal ;;
    2) profile=fast ;;
    3) profile=safe ;;
    4) profile=completion ;;
    *) profile=smart ;;
  esac
  _bash_autosuggestions_apply_profile "$profile"

  printf '%sLoaded profile:%s %s\n' "$_bas_c_ok" "$_bas_c_reset" "$profile"
  _bash_autosuggestions_config_summary

  _bash_autosuggestions_select_setting 'Customize This Profile?' yes \
    yes 'yes  edit individual settings' \
    no 'no   use this profile as-is'
  [[ $_bas_selected_value == no ]] && return 0

  _bash_autosuggestions_set_from_menu _bas_cfg_async 'Async Mode' "$_bas_cfg_async" \
    auto 'auto  async unless the expanded prompt is visibly multi-line' \
    1 '1     always async; best for slow strategies' \
    0 '0     synchronous; simplest behavior' \
    __custom__ 'custom value'

  _bash_autosuggestions_set_from_menu _bas_cfg_strategy 'Strategy Order' "$_bas_cfg_strategy" \
    history 'history' \
    match_prev_cmd 'match_prev_cmd' \
    'match_prev_cmd completion' 'match_prev_cmd completion' \
    'match_prev_cmd history completion' 'match_prev_cmd history completion' \
    completion 'completion' \
    __custom__ 'custom strategy list'

  _bash_autosuggestions_style_gallery
  _bash_autosuggestions_set_from_menu _bas_cfg_style 'Highlight Style' "$_bas_cfg_style" \
    'fg=8' 'fg=8                  default dim ghost text' \
    'fg=244' 'fg=244                lighter gray' \
    'fg=#777777,bold' 'fg=#777777,bold       bold gray' \
    'fg=cyan,underline' 'fg=cyan,underline    cyan underline' \
    'fg=#999999,bg=#202020' 'fg=#999999,bg=#202020  gray on dark background' \
    __custom__ 'custom style string'
  _bash_autosuggestions_preview_style "$_bas_cfg_style"

  _bash_autosuggestions_set_from_menu _bas_cfg_buffer_max 'Buffer Max Size' "$_bas_cfg_buffer_max" \
    '' 'unset  no typed-length limit' \
    80 '80     stop fetching on long lines' \
    120 '120    conservative limit' \
    200 '200    generous limit' \
    __custom__ 'custom number or blank'

  _bash_autosuggestions_set_from_menu _bas_cfg_history_ignore 'History Ignore Pattern' "$_bas_cfg_history_ignore" \
    '' 'unset        use all matching history' \
    '*password*' '*password*   hide password commands' \
    '*token*' '*token*      hide token commands' \
    'sudo *' 'sudo *       hide sudo commands' \
    __custom__ 'custom shell glob'

  _bash_autosuggestions_set_from_menu _bas_cfg_completion_ignore 'Completion Ignore Pattern' "$_bas_cfg_completion_ignore" \
    '' 'unset        use completion suggestions' \
    'git *' 'git *        skip completion for git commands' \
    'ssh *' 'ssh *        skip completion for ssh commands' \
    __custom__ 'custom shell glob'

  _bash_autosuggestions_set_from_menu _bas_cfg_accept_keyseqs 'Accept Keyseqs' "$_bas_cfg_accept_keyseqs" \
    '' 'built-in defaults' \
    '\C-f \e[C' '\C-f \e[C   Ctrl-F and Right' \
    '\C-f' '\C-f        Ctrl-F only' \
    '\e[C' '\e[C        Right only' \
    __custom__ 'custom Readline keyseqs'

  _bash_autosuggestions_set_from_menu _bas_cfg_partial_keyseqs 'Partial Accept Keyseqs' "$_bas_cfg_partial_keyseqs" \
    '' 'built-in defaults' \
    '\ef' '\ef         Alt-F word-at-a-time' \
    '\e[1;5C' '\e[1;5C     Ctrl-Right' \
    __custom__ 'custom Readline keyseqs'

  _bash_autosuggestions_set_from_menu _bas_cfg_execute_keyseqs 'Execute Keyseqs' "$_bas_cfg_execute_keyseqs" \
    '' 'built-in defaults' \
    '\C-j' '\C-j        Ctrl-J' \
    '\C-x\C-e' '\C-x\C-e    Ctrl-X Ctrl-E' \
    __custom__ 'custom Readline keyseqs'

  _bash_autosuggestions_set_from_menu _bas_cfg_clear_keyseqs 'Clear Keyseqs' "$_bas_cfg_clear_keyseqs" \
    '' 'built-in defaults' \
    '\C-g' '\C-g        Ctrl-G' \
    '\C-c' '\C-c        Ctrl-C' \
    __custom__ 'custom Readline keyseqs'

  _bash_autosuggestions_config_summary
}

_bash_autosuggestions_configure_help() {
  _bash_autosuggestions_setup_colors
  printf '\n%s%s%s\n' "$_bas_c_title" 'bash_autosuggestions config' "$_bas_c_reset"
  printf '  %sGuided configuration for profiles, strategies, colors, and keys.%s\n\n' "$_bas_c_dim" "$_bas_c_reset"
  printf '%sUsage%s\n' "$_bas_c_heading" "$_bas_c_reset"
  printf '  %sbash_autosuggestions config%s [options]\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %sbash_autosuggestions configure%s [options]\n\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '%sOptions%s\n' "$_bas_c_heading" "$_bas_c_reset"
  printf '  %s-p, --profile NAME%s      use profile: smart, minimal, fast, safe, completion\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %s--print%s                 print the generated ~/.bashrc block and exit\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %s--dry-run%s               preview the block without writing it\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %s-y, --yes%s               write without asking\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %s--no-apply%s              do not apply settings to the current shell\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %s--bashrc PATH%s           write/update PATH instead of ~/.bashrc\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %s--preview-style STR%s     show a highlight preview for STR and exit\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %s--list-profiles%s         list available profiles\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '  %s-h, --help%s              show this help\n\n' "$_bas_c_cmd" "$_bas_c_reset"
  printf '%sInteractive Mode%s\n' "$_bas_c_heading" "$_bas_c_reset"
  printf '  With no options, the configurator opens a cursor-driven menu, previews\n'
  printf '  highlight styles against sample command text, applies settings to this\n'
  printf '  shell, and can update a managed block in ~/.bashrc.\n'
}

_bash_autosuggestions_configure() {
  local profile=smart bashrc=${HOME}/.bashrc print_only=0 dry_run=0 yes=0 apply=1
  local block reply

  while (($#)); do
    case "$1" in
      --profile|-p)
        profile=$2
        shift 2
        ;;
      --profile=*)
        profile=${1#*=}
        shift
        ;;
      --bashrc)
        bashrc=$2
        shift 2
        ;;
      --bashrc=*)
        bashrc=${1#*=}
        shift
        ;;
      --print)
        print_only=1
        shift
        ;;
      --dry-run)
        dry_run=1
        apply=0
        shift
        ;;
      --yes|-y)
        yes=1
        shift
        ;;
      --no-apply)
        apply=0
        shift
        ;;
      --preview-style)
        _bash_autosuggestions_preview_style "$2"
        return 0
        ;;
      --preview-style=*)
        _bash_autosuggestions_preview_style "${1#*=}"
        return 0
        ;;
      --list-profiles)
        printf '%s\n' smart minimal fast safe completion
        return 0
        ;;
      --help|-h)
        _bash_autosuggestions_configure_help
        return 0
        ;;
      *)
        printf 'bash-autosuggestions: unknown configure option: %s\n' "$1" >&2
        _bash_autosuggestions_configure_help >&2
        return 2
        ;;
    esac
  done

  if ! _bash_autosuggestions_apply_profile "$profile"; then
    printf 'bash-autosuggestions: unknown profile: %s\n' "$profile" >&2
    return 2
  fi

  _bas_cfg_loader=${_BASH_AUTOSUGGESTIONS_LOADER:-bash-autosuggestions.bash}
  if [[ $print_only -eq 0 && $dry_run -eq 0 && $yes -eq 0 && -t 0 && -t 1 ]]; then
    _bash_autosuggestions_configure_interactive "$profile"
  fi

  block=$(_bash_autosuggestions_config_block)
  if [[ $print_only -eq 1 ]]; then
    printf '%s\n' "$block"
    return 0
  fi

  printf '\nGenerated configuration:\n\n%s\n' "$block"

  if [[ $apply -eq 1 ]]; then
    _bash_autosuggestions_apply_current
    printf '\nApplied settings to the current shell.\n'
  fi

  if [[ $dry_run -eq 1 ]]; then
    printf 'Dry run: not writing %s.\n' "$bashrc"
    return 0
  fi

  if [[ $yes -eq 0 ]]; then
    printf 'Write/update managed block in %s? [Y/n]: ' "$bashrc"
    IFS= read -r reply
    case "$reply" in
      n|N|no|NO)
        printf 'Skipped writing %s.\n' "$bashrc"
        return 0
        ;;
    esac
  fi

  if _bash_autosuggestions_write_block "$bashrc" "$block"; then
    printf 'Updated %s.\n' "$bashrc"
  else
    printf 'bash-autosuggestions: failed to update %s\n' "$bashrc" >&2
    return 1
  fi
}

bash_autosuggestions() {
  case "${1-}" in
    ''|-h|--help|help)
      _bash_autosuggestions_help
      ;;
    config|configure)
      shift
      _bash_autosuggestions_configure "$@"
      ;;
    *)
      builtin bash_autosuggestions "$@"
      ;;
  esac
}

builtin bash_autosuggestions enable
unset _bash_autosuggestions_dir _bash_autosuggestions_source
