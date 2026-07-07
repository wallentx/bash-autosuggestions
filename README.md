# bash-autosuggestions

Fish-like autosuggestions for Bash, modeled on `zsh-autosuggestions`.

This is a Bash loadable builtin. It hooks GNU Readline directly so suggestions
are drawn as muted trailing text after the cursor, like zsh's `POSTDISPLAY`.

## Build

```sh
make
```

## Use

```sh
source /path/to/bash-autosuggestions.bash
```

Or load manually:

```sh
enable -f /path/to/bash-autosuggestions.so bash_autosuggestions
bash_autosuggestions enable
```

## Behavior

- Suggestions appear after the cursor in `fg=8` by default.
- The default strategy is `history`, matching the most recent command with the
  current buffer as a prefix.
- Right arrow, `Ctrl-F`, End, and `Ctrl-E` accept the whole suggestion when the
  cursor is at the end of the buffer.
- `Alt-F` partially accepts the next shell word.
- Bash vi-command mode mirrors the zsh behavior for common motions: `l`/space
  accept the whole suggestion from the end-equivalent cursor position, while
  `w`, `e`, and `f<char>` partially accept through the cursor movement.
- Bracketed paste clears stale ghost text before Readline inserts pasted text,
  matching zsh-autosuggestions' bracketed-paste integration behavior.
- Exposed Readline commands:
  - `autosuggest-accept`
  - `autosuggest-execute`
  - `autosuggest-clear`
  - `autosuggest-fetch`
  - `autosuggest-enable`
  - `autosuggest-disable`
  - `autosuggest-toggle`
  - `autosuggest-forward-char`
  - `autosuggest-end-of-line`
  - `autosuggest-forward-word`
- `autosuggest-fetch` mirrors zsh-autosuggestions: it can fetch and display a
  suggestion even while automatic suggestions are disabled. Typing into that
  fetched suggestion keeps accepting it character by character.

Bind any of the exposed commands with Bash's `bind` builtin:

```sh
bind '"\C-b": autosuggest-toggle'
bind '"\C- ": autosuggest-accept'
```

These commands mirror the zsh widgets of the same names: accept inserts the
current suggestion, execute inserts and submits it, clear removes only the
current ghost text, and enable/disable/toggle control automatic fetching.

## Configuration

The Bash-prefixed variables are preferred. The matching
`ZSH_AUTOSUGGEST_*` names are also accepted for compatibility.

```sh
BASH_AUTOSUGGEST_HIGHLIGHT_STYLE='fg=8'
BASH_AUTOSUGGEST_STRATEGY='history'
# or Bash/zsh-style arrays:
BASH_AUTOSUGGEST_STRATEGY=(history completion)
ZSH_AUTOSUGGEST_STRATEGY=(history completion)
BASH_AUTOSUGGEST_BUFFER_MAX_SIZE=
BASH_AUTOSUGGEST_HISTORY_IGNORE=
BASH_AUTOSUGGEST_COMPLETION_IGNORE=
BASH_AUTOSUGGEST_USE_ASYNC=1
BASH_AUTOSUGGEST_ACCEPT_KEYSEQS=
BASH_AUTOSUGGEST_EXECUTE_KEYSEQS=
BASH_AUTOSUGGEST_CLEAR_KEYSEQS=
BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS=
```

The matching `ZSH_AUTOSUGGEST_*` names work as aliases for the same settings.
For keyseq settings, use Readline key sequence syntax such as `\C-b`, then run
`bash_autosuggestions bind` to apply the mapping.
Async fetching is opt-in. Set `BASH_AUTOSUGGEST_USE_ASYNC=1` or
`ZSH_AUTOSUGGEST_USE_ASYNC=1` before loading or enabling the builtin to fetch
suggestions in a worker process.

Strategies are tried left to right:

- `history`
- `match_prev_cmd`
- `completion` (uses Bash/Readline completion data and displays the first
  completion result, matching the zsh-autosuggestions strategy)
- Custom strategies named in `BASH_AUTOSUGGEST_STRATEGY` or
  `ZSH_AUTOSUGGEST_STRATEGY` call shell functions named
  `_bash_autosuggest_strategy_NAME` or `_zsh_autosuggest_strategy_NAME`.
  Like zsh-autosuggestions, the function receives the current buffer as `$1`
  and should set `suggestion`.

Example:

```sh
BASH_AUTOSUGGEST_STRATEGY='match_prev_cmd history completion'
BASH_AUTOSUGGEST_HIGHLIGHT_STYLE='fg=#777777'
```

## Status

This repo starts from the same behavioral contract as `zsh-autosuggestions`,
but implements it through Readline rather than zle. Bash does not have zsh's
widget or region-highlighting API, so this plugin uses a native Readline
redisplay hook and loadable-builtin entrypoints.

See `PARITY.md` for the spec-by-spec audit against `../zsh-autosuggestions`.
