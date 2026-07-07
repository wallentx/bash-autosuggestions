# bash-autosuggestions

Fish-like autosuggestions for Bash, modeled on `zsh-autosuggestions`.

This is a Bash loadable builtin. It hooks GNU Readline directly so suggestions
are drawn as muted trailing text after the cursor, like zsh's `POSTDISPLAY`.

## Build

```sh
make all
```

Run `make` or `make help` to see the available build, test, install, and
startup-file helper targets.

## Install

```sh
make install
```

This installs the loadable builtin and loader script under:

```sh
/usr/local/lib/bash-autosuggestions/
```

It then prints the `~/.bashrc` line needed to load the plugin and, when run
interactively, asks whether to add/update that line for you.
When run through `sudo`, the installer targets the invoking user's `~/.bashrc`
instead of `/root/.bashrc`.
After installation, source your bash startup file or open a new terminal. The
`bash_autosuggestions` command is available after the loader is sourced.

Useful install options:

```sh
make install PREFIX="$HOME/.local"
make install BASHRC_UPDATE=yes
make install BASHRC_UPDATE=no
make bashrc-snippet
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

## Configure

After sourcing the loader, run the guided configurator:

```sh
bash_autosuggestions config
```

`bash_autosuggestions` with no arguments prints the command help. `config`
opens the guided configurator; `configure` is kept as an alias. The
configurator presents a cursor-driven menu of profiles, explains each setting,
previews highlight styles against sample command text, applies the selected
settings to the current shell, and can write/update a managed block in
`~/.bashrc`.

Useful non-interactive forms:

```sh
bash_autosuggestions config --list-profiles
bash_autosuggestions config --preview-style 'fg=#777777,bold'
bash_autosuggestions config --profile smart --print
bash_autosuggestions config --profile safe --dry-run
bash_autosuggestions config --profile smart --yes
```

Profiles:

- `smart`: `match_prev_cmd completion`, `async=auto`, word-at-a-time accept on
  `Alt-F`
- `minimal`: history only, `async=auto`
- `fast`: history only, `async=1`
- `safe`: history only, `async=0`, conservative size/ignore defaults
- `completion`: aggressive `match_prev_cmd history completion`

The configurator-generated `~/.bashrc` block is bounded by:

```sh
# >>> bash-autosuggestions >>>
source /usr/local/lib/bash-autosuggestions/bash-autosuggestions.bash
# <<< bash-autosuggestions <<<
```

Rerunning `bash_autosuggestions config` replaces that block rather than
appending duplicates.

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
BASH_AUTOSUGGEST_USE_ASYNC=auto
BASH_AUTOSUGGEST_ACCEPT_KEYSEQS=
BASH_AUTOSUGGEST_EXECUTE_KEYSEQS=
BASH_AUTOSUGGEST_CLEAR_KEYSEQS=
BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS=
```

The matching `ZSH_AUTOSUGGEST_*` names work as aliases for the same settings.
For keyseq settings, use Readline key sequence syntax such as `\C-b`, then run
`bash_autosuggestions bind` to apply the mapping.

Async fetching is enabled by default when neither `BASH_AUTOSUGGEST_USE_ASYNC`
nor `ZSH_AUTOSUGGEST_USE_ASYNC` is set. It keeps the prompt responsive while
slower strategies, custom functions, or completion lookups run in a worker
process. Set the value at runtime:

- unset: async enabled
- `1`, `yes`, `true`, `on`, or any other non-empty non-false value: async
  enabled
- `0`, `no`, `false`, or `off`: async disabled
- `auto`: async enabled unless the expanded Readline prompt is visibly
  multi-line

Use `0` for fully synchronous fetching, or `auto` when a multi-line prompt has
redraw artifacts with async suggestion updates.

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
