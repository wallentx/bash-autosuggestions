# zsh-autosuggestions parity audit

Audit source: `../zsh-autosuggestions/spec`.

This port targets the user-visible autosuggestion behavior in Bash. It does not
try to emulate zsh internals that Bash does not have, such as zle widget
registries, `POSTDISPLAY`, zpty completion capture, or zsh shell options. Bash
equivalents use GNU Readline hooks, loadable builtin commands, and key sequence
bindings.

## Spec map

| Upstream spec | Bash status |
| --- | --- |
| `multi_line_spec.rb` | Covered. Suggestions containing line feeds render over multiple terminal lines and save/restore cursor position. |
| `line_init_spec.rb` | Equivalent hook implemented. Bash uses `rl_pre_input_hook` to fetch at prompt initialization; zsh's `zle-line-init` widget namespace is not present in Bash. |
| `kill_ring_spec.rb` | Covered. Readline kill/yank/yank-pop keeps cycling while autosuggestions are active. |
| `widgets/enable_spec.rb` | Covered. `autosuggest-enable` enables fetching, fetches on non-empty buffers, and stays quiet on empty buffers. |
| `widgets/disable_spec.rb` | Covered. `autosuggest-disable` disables automatic fetching and clears visible suggestion. |
| `widgets/fetch_spec.rb` | Covered. `autosuggest-fetch` works even when automatic suggestions are disabled. |
| `widgets/toggle_spec.rb` | Covered. `autosuggest-toggle` disables, re-enables, and refreshes current suggestions. |
| `async_spec.rb` | Covered for Bash-visible behavior. Async is default-on, does not block delayed strategies, does not leak worker stdout/stderr, keeps Bash history navigation working, and returns cleanly after `Ctrl-C`. zsh-only `copy-earlier-word` widget behavior has no direct Bash widget equivalent; Readline kill/yank behavior is covered separately. |
| `integrations/bracketed_paste_magic_spec.rb` | Covered. Bracketed paste clears stale ghost text before Readline inserts pasted text, including while suggestions are disabled. |
| `integrations/zle_input_stack_spec.rb` | zle-only. Bash has no `zle -U`; the closest Bash risk, stale queued async results, is handled by exact-prefix async response validation. |
| `integrations/auto_cd_spec.rb` | zsh option only. Bash completion strategy calls Readline completion directly and does not alter directory completion behavior. |
| `integrations/glob_subst_spec.rb` | zsh option only. Prefix matching is literal `strncmp`; special characters in buffers are covered. |
| `integrations/client_zpty_spec.rb` | zsh-only. Bash completion strategy does not create or use zpty. |
| `integrations/vi_mode_spec.rb` | Covered. Bash vi command mode accepts at zsh's end-equivalent cursor position, partially accepts `w`, `e`, and `f<char>`, preserves suggestions on left movement, and keeps `dl` delete behavior working. |
| `integrations/wrapped_widget_spec.rb` | zle-only. Bash cannot wrap arbitrary zle widgets; equivalent user-facing commands are exposed as Readline commands and configurable key sequences. |
| `integrations/rebound_bracket_spec.rb` | zle-only. Bash key rebinding is handled through Readline `bind` and `bash_autosuggestions bind`; no zle bracket widget exists. |
| `strategies/history_spec.rb` | Covered. Most recent matching history entry is suggested, with `*_HISTORY_IGNORE` pattern support. |
| `strategies/match_prev_cmd_spec.rb` | Covered. Previous-command-aware history matching and ignore patterns are implemented. |
| `strategies/completion_spec.rb` | Covered. First completion result is suggested, common-prefix slot is skipped, ignore patterns are honored, programmable completion works, async completion works, and literal newline prefixes do not add extra carriage returns. |
| `strategies/special_characters_helper.rb` | Covered. Literal prefix handling is tested for `*`, `?`, backslash, double backslash, `~`, `$()`, square brackets, `#`, `^`, and leading `-`. |
| `options/strategy_spec.rb` | Covered. Scalar and Bash/zsh-style array strategy settings are supported; custom Bash and zsh-compatible strategy function names are supported. |
| `options/highlight_style_spec.rb` | Covered. Default `fg=8` and zle-style color attributes such as hex foreground, background color, bold, underline, and standout are mapped to ANSI. |
| `options/buffer_max_size_spec.rb` | Covered. Empty max allows all; shorter/equal buffers fetch; oversized buffers suppress suggestions. |
| `options/widget_lists_spec.rb` | Bash equivalent implemented. zsh widget arrays cannot map directly to Bash, so this port exposes Readline commands and key sequence config variables for accept, execute, clear, and partial-accept behavior. `bash_autosuggestions bind` applies changes explicitly. |
| `options/original_widget_prefix_spec.rb` | zle-only. Bash has no zle original-widget namespace; default Readline fallbacks are stored as C function pointers for the built-in bindings. |

## Verification

Run:

```sh
make clean
make CFLAGS='-O2 -g -Wall -Wextra -Werror'
make test
```

The pty test suite loads the builtin into an interactive Bash process and
checks real terminal output, key bindings, async workers, vi mode, bracketed
paste, completion, custom strategies, and multiline rendering.
