#!/usr/bin/env python3
import fcntl
import os
import select
import shlex
import shutil
import struct
import termios
import tempfile
import time


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROMPT = b"bas-test$ "


def read_until(fd, needle, timeout=5.0):
    deadline = time.time() + timeout
    data = b""
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        data += chunk
        if needle in data:
            return data
    raise AssertionError(f"timed out waiting for {needle!r}; saw {data!r}")


def write(fd, data):
    os.write(fd, data)


def read_available(fd, timeout=0.5):
    deadline = time.time() + timeout
    data = b""
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        data += chunk
    return data


def set_pty_size(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def cursor_after_wrapped_suggestion(data, cols, start_col):
    row = 0
    col = start_col
    saved = None
    before_restore = None
    after_restore = None
    max_row_after_save = 0
    i = 0
    while i < len(data):
        if data.startswith(b"\x1b[s", i):
            saved = (row, col)
            max_row_after_save = row
            i += 3
            continue
        if data.startswith(b"\x1b[u", i):
            before_restore = (row, col)
            if saved is not None:
                row, col = saved
            after_restore = (row, col)
            i += 3
            continue
        if data[i:i + 2] == b"\x1b[":
            j = i + 2
            while j < len(data) and not (0x40 <= data[j] <= 0x7e):
                j += 1
            if j >= len(data):
                break
            params = data[i + 2:j].decode(errors="ignore")
            final = chr(data[j])
            first = params.split(";")[0] if params else ""
            amount = int(first) if first.isdigit() else 1
            if final == "C":
                col += amount
            elif final == "D":
                col = max(0, col - amount)
            elif final == "G":
                col = max(0, amount - 1)
            elif final in ("H", "f"):
                row = 0
                col = 0
            i = j + 1
            continue
        byte = data[i]
        if byte == 8:
            col = max(0, col - 1)
        elif byte == 13:
            col = 0
        elif byte == 10:
            row += 1
        elif byte >= 0x20 and byte != 0x7f:
            col += 1
            if col >= cols:
                row += 1
                col = 0
        if saved is not None and before_restore is None:
            max_row_after_save = max(max_row_after_save, row)
        i += 1
    return saved, before_restore, after_restore, max_row_after_save


def run(fd, command):
    write(fd, command.encode() + b"\n")
    return read_until(fd, PROMPT)


def clear_line(fd):
    write(fd, b"\x15\n")
    read_until(fd, PROMPT)
    read_available(fd, timeout=0.05)


def cancel_line(fd):
    write(fd, b"\x03")
    try:
        read_until(fd, PROMPT, timeout=1.0)
    except AssertionError:
        write(fd, b"\n")
        read_until(fd, PROMPT)
    read_available(fd, timeout=0.05)


def reset_session(fd):
    clear_line(fd)
    run(fd, "bind '\"\\C-b\": self-insert'")
    run(fd, "bind '\"\\C-f\": autosuggest-forward-char'")
    run(fd, "bind '\"\\C-g\": abort'")
    run(fd, "BASH_AUTOSUGGEST_STRATEGY=history")
    run(fd, "unset BASH_AUTOSUGGEST_HIGHLIGHT_STYLE ZSH_AUTOSUGGEST_HIGHLIGHT_STYLE")
    run(fd, "unset BASH_AUTOSUGGEST_HISTORY_IGNORE ZSH_AUTOSUGGEST_HISTORY_IGNORE")
    run(fd, "unset BASH_AUTOSUGGEST_BUFFER_MAX_SIZE ZSH_AUTOSUGGEST_BUFFER_MAX_SIZE")
    run(fd, "unset BASH_AUTOSUGGEST_COMPLETION_IGNORE ZSH_AUTOSUGGEST_COMPLETION_IGNORE")
    run(fd, "unset BASH_AUTOSUGGEST_USE_ASYNC ZSH_AUTOSUGGEST_USE_ASYNC")
    run(fd, "unset BASH_AUTOSUGGEST_ACCEPT_KEYSEQS ZSH_AUTOSUGGEST_ACCEPT_KEYSEQS")
    run(fd, "unset BASH_AUTOSUGGEST_EXECUTE_KEYSEQS ZSH_AUTOSUGGEST_EXECUTE_KEYSEQS")
    run(fd, "unset BASH_AUTOSUGGEST_CLEAR_KEYSEQS ZSH_AUTOSUGGEST_CLEAR_KEYSEQS")
    run(fd, "unset BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS ZSH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS")
    run(fd, "bash_autosuggestions enable")
    run(fd, "history -c")


def main():
    tmp_parent = os.environ.get("TMPDIR") or ROOT
    tmpdir = tempfile.mkdtemp(prefix="bash-autosuggestions-", dir=tmp_parent)
    pid, fd = os.forkpty()
    if pid == 0:
        os.chdir(tmpdir)
        os.environ["TERM"] = "xterm-256color"
        os.environ["PS1"] = PROMPT.decode()
        os.environ["HISTFILE"] = os.path.join(tmpdir, "history")
        os.execlp("bash", "bash", "--noprofile", "--norc", "-i")

    try:
        read_until(fd, PROMPT)
        run(fd, f"enable -f {shlex.quote(os.path.join(ROOT, 'bash-autosuggestions.so'))} bash_autosuggestions")
        run(fd, "bash_autosuggestions enable")
        async_default = run(fd, "case ${BASH_AUTOSUGGEST_USE_ASYNC+x} in x) echo async-default;; *) echo async-missing;; esac")
        if b"async-default" not in async_default:
            raise AssertionError(f"async was not enabled by default: {async_default!r}")
        run(fd, "set +o history")
        run(fd, "history -c")

        run(fd, "history -s 'echo hello from autosuggestions'")
        write(fd, b"ec")
        rendered = read_until(fd, b"ho hello from autosuggestions")
        if b"\x1b[38;5;8mho hello from autosuggestions\x1b[0m" not in rendered:
            raise AssertionError(f"suggestion was not rendered with fg=8 style: {rendered!r}")

        write(fd, b"\x1b[C\n")
        output = read_until(fd, b"hello from autosuggestions")
        if b"echo hello from autosuggestions" not in output:
            raise AssertionError(f"right arrow did not accept full suggestion: {output!r}")
        if PROMPT not in output:
            read_until(fd, PROMPT)

        run(fd, "history -s 'git checkout main'")
        write(fd, b"git ")
        read_until(fd, b"\x1b[38;5;8mcheckout main\x1b[0m")
        write(fd, b"\x1bf")
        read_until(fd, b"\x1b[38;5;8m main\x1b[0m")
        clear_line(fd)

        run(fd, "BASH_AUTOSUGGEST_STRATEGY=match_prev_cmd")
        run(fd, "history -c")
        run(fd, "history -s pwd")
        run(fd, "history -s 'ls foo'")
        run(fd, "history -s 'ls bar'")
        run(fd, "history -s pwd")
        write(fd, b"ls")
        rendered = read_until(fd, b"\x1b[38;5;8m foo\x1b[0m")
        if b"\x1b[38;5;8m bar\x1b[0m" in rendered:
            raise AssertionError(f"match_prev_cmd chose newest history match instead: {rendered!r}")
        clear_line(fd)

        run(fd, "BASH_AUTOSUGGEST_STRATEGY=history")
        run(fd, "BASH_AUTOSUGGEST_HISTORY_IGNORE='* bar'")
        run(fd, "history -c")
        run(fd, "history -s 'ls foo'")
        run(fd, "history -s 'ls bar'")
        run(fd, "history -s 'echo baz'")
        write(fd, b"ls")
        read_until(fd, b"\x1b[38;5;8m foo\x1b[0m")
        clear_line(fd)
        run(fd, "unset BASH_AUTOSUGGEST_HISTORY_IGNORE")

        run(fd, "history -c")
        run(fd, "history -s 'echo \"hello*\"'")
        run(fd, "history -s 'echo \"hello.\"'")
        write(fd, b'echo "hello*')
        read_until(fd, b"\x1b[38;5;8m\"\x1b[0m")
        clear_line(fd)

        run(fd, "history -c")
        run(fd, "history -s 'echo \"$history[123]\"'")
        write(fd, b'echo "$history[')
        read_until(fd, b"\x1b[38;5;8m123]\"\x1b[0m")
        write(fd, b"123]")
        read_until(fd, b"\x1b[38;5;8m\"\x1b[0m")
        clear_line(fd)

        special_cases = [
            ("echo \"hello?\"", b'echo "hello?', b'"\x1b[0m'),
            ("echo \"hello\\nworld\"", b'echo "hello\\', b'nworld"\x1b[0m'),
            ("echo \"\\\\\\\\\"", b'echo "\\\\', b'\\\\"\x1b[0m'),
            ("echo ~/foo", b"echo ~", b"/foo\x1b[0m"),
            ("echo \"$(ls foo)\"", b'echo "$(', b'ls foo)"\x1b[0m'),
            ("echo \"#yolo\"", b'echo "#', b'yolo"\x1b[0m'),
            ("echo \"^A\"", b'echo "^A', b'"\x1b[0m'),
            ("-foo() {}", b"-", b"foo() {}\x1b[0m"),
        ]
        for history_line, typed, suffix in special_cases:
            run(fd, "history -c")
            history_cmd = "history -s"
            if history_line.startswith("-"):
                history_cmd = "history -s --"
            run(fd, f"{history_cmd} {shlex.quote(history_line)}")
            write(fd, typed)
            read_until(fd, b"\x1b[38;5;8m" + suffix)
            clear_line(fd)

        run(fd, "history -c")
        run(fd, "history -s 'echo abcdefghijklmnopqrstuvwxyz'")
        run(fd, "BASH_AUTOSUGGEST_BUFFER_MAX_SIZE=10")
        write(fd, b"echo abcd")
        read_until(fd, b"\x1b[38;5;8mefghijklmnopqrstuvwxyz\x1b[0m")
        clear_line(fd)
        write(fd, b"echo abcde")
        read_until(fd, b"\x1b[38;5;8mfghijklmnopqrstuvwxyz\x1b[0m")
        clear_line(fd)
        write(fd, b"echo abcdef")
        oversized = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m" in oversized:
            raise AssertionError(f"buffer max did not suppress oversized prefix: {oversized!r}")
        clear_line(fd)
        run(fd, "unset BASH_AUTOSUGGEST_BUFFER_MAX_SIZE")

        run(fd, "BASH_AUTOSUGGEST_STRATEGY=completion")
        run(fd, "touch autosuggest-target-file")
        write(fd, b"cat autosugg")
        read_until(fd, b"\x1b[38;5;8mest-target-file\x1b[0m")
        clear_line(fd)

        run(fd, "complete -W 'bar bat' baz")
        write(fd, b"baz ")
        completion = read_until(fd, b"\x1b[38;5;8mbar\x1b[0m")
        if b"\x1b[38;5;8mba\x1b[0m" in completion:
            raise AssertionError(f"completion strategy used common prefix, not first result: {completion!r}")
        clear_line(fd)

        write(fd, b"baz \\\x16\n")
        newline_completion = read_until(fd, b"\x1b[38;5;8mbar\x1b[0m")
        if b"\r\r\n" in newline_completion:
            raise AssertionError(
                f"completion strategy added an extra carriage return: {newline_completion!r}"
            )
        cancel_line(fd)
        reset_session(fd)
        run(fd, "BASH_AUTOSUGGEST_STRATEGY=completion")
        run(fd, "complete -W 'bar bat' baz")

        run(fd, "BASH_AUTOSUGGEST_STRATEGY=(completion)")
        write(fd, b"baz ")
        read_until(fd, b"\x1b[38;5;8mbar\x1b[0m")
        clear_line(fd)

        run(fd, "unset BASH_AUTOSUGGEST_STRATEGY")
        run(fd, "ZSH_AUTOSUGGEST_STRATEGY=(completion)")
        write(fd, b"baz ")
        read_until(fd, b"\x1b[38;5;8mbar\x1b[0m")
        clear_line(fd)
        run(fd, "unset ZSH_AUTOSUGGEST_STRATEGY")

        run(fd, "BASH_AUTOSUGGEST_STRATEGY=(completion history)")
        run(fd, "history -s 'zz array fallback'")
        write(fd, b"zz")
        read_until(fd, b"\x1b[38;5;8m array fallback\x1b[0m")
        clear_line(fd)

        run(fd, "BASH_AUTOSUGGEST_USE_ASYNC=1")
        run(fd, "BASH_AUTOSUGGEST_STRATEGY=completion")
        run(fd, "complete -W 'worker-one worker-two' worker-cmd")
        write(fd, b"worker-cmd ")
        read_until(fd, b"\x1b[38;5;8mworker-one\x1b[0m")
        clear_line(fd)

        write(fd, b"baz \\\x16\n")
        async_newline_completion = read_until(fd, b"\x1b[38;5;8mbar\x1b[0m")
        if b"\r\r\n" in async_newline_completion:
            raise AssertionError(
                f"async completion strategy added an extra carriage return: {async_newline_completion!r}"
            )
        cancel_line(fd)
        reset_session(fd)
        run(fd, "BASH_AUTOSUGGEST_USE_ASYNC=1")

        run(fd, "history -c")
        run(fd, "history -s 'echo foo'")
        run(fd, "history -s 'echo bar'")
        run(fd, "history -s 'echo baz'")
        write(fd, b"\x1b[A\x1b[A\x1b[A")
        read_until(fd, b"echo foo")
        clear_line(fd)

        run(fd, "history -c")
        run(fd, "history -s 'echo async-interrupt'")
        write(fd, b"e")
        read_until(fd, b"\x1b[38;5;8mcho async-interrupt\x1b[0m")
        cancel_line(fd)
        write(fd, b"echo after-interrupt\n")
        output = read_until(fd, b"after-interrupt")
        if b"async-interrupt" in output:
            raise AssertionError(f"async suggestion survived interrupt: {output!r}")
        run(fd, "unset BASH_AUTOSUGGEST_USE_ASYNC")

        run(fd, "_bash_autosuggest_strategy_foobar() { [[ 'foobar baz' == \"$1\"* ]] && suggestion='foobar baz'; }")
        run(fd, "_bash_autosuggest_strategy_foobaz() { [[ 'foobaz bar' == \"$1\"* ]] && suggestion='foobaz bar'; }")
        run(fd, "BASH_AUTOSUGGEST_STRATEGY=(foobar foobaz)")
        write(fd, b"foo")
        read_until(fd, b"\x1b[38;5;8mbar baz\x1b[0m")
        write(fd, b"baz")
        read_until(fd, b"\x1b[38;5;8m bar\x1b[0m")
        clear_line(fd)

        run(fd, "unset -f _bash_autosuggest_strategy_foobar _bash_autosuggest_strategy_foobaz")
        run(fd, "_zsh_autosuggest_strategy_legacy() { [[ 'legacy works' == \"$1\"* ]] && suggestion='legacy works'; }")
        run(fd, "unset BASH_AUTOSUGGEST_STRATEGY")
        run(fd, "ZSH_AUTOSUGGEST_STRATEGY=(legacy)")
        write(fd, b"leg")
        read_until(fd, b"\x1b[38;5;8macy works\x1b[0m")
        clear_line(fd)
        run(fd, "unset ZSH_AUTOSUGGEST_STRATEGY")
        run(fd, "unset -f _zsh_autosuggest_strategy_legacy")

        run(fd, "BASH_AUTOSUGGEST_USE_ASYNC=1")
        run(fd, "_bash_autosuggest_strategy_slow() { sleep 0.2; [[ 'async works' == \"$1\"* ]] && suggestion='async works'; }")
        run(fd, "BASH_AUTOSUGGEST_STRATEGY=(slow)")
        write(fd, b"as")
        early_async = read_available(fd, timeout=0.05)
        if b"\x1b[38;5;8m" in early_async:
            raise AssertionError(f"async strategy rendered before delayed worker returned: {early_async!r}")
        read_until(fd, b"\x1b[38;5;8mync works\x1b[0m")
        clear_line(fd)
        run(fd, "unset BASH_AUTOSUGGEST_USE_ASYNC")
        run(fd, "unset -f _bash_autosuggest_strategy_slow")

        run(fd, "BASH_AUTOSUGGEST_USE_ASYNC=1")
        run(fd, "_bash_autosuggest_strategy_noisy() { printf 'NOISY-STDOUT'; printf 'NOISY-STDERR' >&2; [[ 'noise clean' == \"$1\"* ]] && suggestion='noise clean'; }")
        run(fd, "BASH_AUTOSUGGEST_STRATEGY=(noisy)")
        write(fd, b"noi")
        noisy = read_until(fd, b"\x1b[38;5;8mse clean\x1b[0m")
        if b"NOISY-STDOUT" in noisy or b"NOISY-STDERR" in noisy:
            raise AssertionError(f"async worker leaked strategy output: {noisy!r}")
        clear_line(fd)
        run(fd, "unset BASH_AUTOSUGGEST_USE_ASYNC")
        run(fd, "unset -f _bash_autosuggest_strategy_noisy")

        run(fd, "BASH_AUTOSUGGEST_COMPLETION_IGNORE='baz *'")
        write(fd, b"baz ")
        ignored_completion = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m" in ignored_completion:
            raise AssertionError(f"completion ignore pattern was not respected: {ignored_completion!r}")
        run(fd, "unset BASH_AUTOSUGGEST_COMPLETION_IGNORE")
        reset_session(fd)

        run(fd, "bind '\"\\C-b\": autosuggest-disable'")
        run(fd, "history -s 'echo hello'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;5;8m hello\x1b[0m")
        write(fd, b"\x02")
        disabled = read_until(fd, b"echo")
        if b"\x1b[38;5;8m hello\x1b[0m" in disabled:
            raise AssertionError(f"disable did not clear suggestion: {disabled!r}")
        write(fd, b" h")
        idle = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m" in idle:
            raise AssertionError(f"disabled autosuggest fetched new suggestion: {idle!r}")
        reset_session(fd)

        run(fd, "bind '\"\\C-b\": autosuggest-disable'")
        run(fd, "bind '\"\\C-f\": autosuggest-fetch'")
        run(fd, "history -s 'echo hello'")
        write(fd, b"\x02")
        write(fd, b"echo h")
        before_fetch = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m" in before_fetch:
            raise AssertionError(f"disabled state displayed suggestion before fetch: {before_fetch!r}")
        write(fd, b"\x06")
        read_until(fd, b"\x1b[38;5;8mello\x1b[0m")
        write(fd, b"e")
        read_until(fd, b"\x1b[38;5;8mllo\x1b[0m")
        reset_session(fd)

        run(fd, "bind '\"\\C-b\": autosuggest-toggle'")
        run(fd, "history -s 'echo world'")
        run(fd, "history -s 'echo hello'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;5;8m hello\x1b[0m")
        write(fd, b"\x02 h")
        toggled_off = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m" in toggled_off:
            raise AssertionError(f"toggle-off still fetched suggestion: {toggled_off!r}")
        write(fd, b"\x02")
        read_until(fd, b"\x1b[38;5;8mello\x1b[0m")
        write(fd, b"\x7f")
        write(fd, b"w")
        read_until(fd, b"\x1b[38;5;8morld\x1b[0m")
        reset_session(fd)

        run(fd, "bind '\"\\C-b\": autosuggest-accept'")
        run(fd, "history -s 'echo accept-widget'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;5;8m accept-widget\x1b[0m")
        write(fd, b"\x02\n")
        output = read_until(fd, b"accept-widget")
        if b"echo accept-widget" not in output:
            raise AssertionError(f"autosuggest-accept did not accept buffer before newline: {output!r}")
        if PROMPT not in output:
            read_until(fd, PROMPT)
        reset_session(fd)

        run(fd, "bind '\"\\C-b\": autosuggest-execute'")
        run(fd, "history -s 'echo execute-widget'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;5;8m execute-widget\x1b[0m")
        write(fd, b"\x02")
        output = read_until(fd, b"execute-widget")
        if b"echo execute-widget" not in output:
            raise AssertionError(f"autosuggest-execute did not accept and execute: {output!r}")
        if PROMPT not in output:
            read_until(fd, PROMPT)
        reset_session(fd)

        run(fd, "bind '\"\\C-b\": autosuggest-clear'")
        run(fd, "history -s 'echo clear-widget'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;5;8m clear-widget\x1b[0m")
        write(fd, b"\x02")
        cleared = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m clear-widget\x1b[0m" in cleared:
            raise AssertionError(f"autosuggest-clear did not clear visible suggestion: {cleared!r}")
        reset_session(fd)

        run(fd, "bind '\"\\C-b\": autosuggest-enable'")
        run(fd, "bind '\"\\C-g\": autosuggest-disable'")
        run(fd, "history -s 'echo enable-widget'")
        write(fd, b"\x07")
        write(fd, b"e")
        disabled = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m" in disabled:
            raise AssertionError(f"autosuggest-disable did not suppress suggestion before enable: {disabled!r}")
        write(fd, b"\x02")
        read_until(fd, b"\x1b[38;5;8mcho enable-widget\x1b[0m")
        reset_session(fd)

        run(fd, "ZSH_AUTOSUGGEST_HIGHLIGHT_STYLE='fg=#ff00ff,bg=cyan,bold,underline'")
        run(fd, "history -s 'echo styled-widget'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;2;255;0;255;48;5;6;1;4m styled-widget\x1b[0m")
        reset_session(fd)

        run(fd, "BASH_AUTOSUGGEST_ACCEPT_KEYSEQS='\\C-b'")
        run(fd, "bash_autosuggestions bind")
        run(fd, "history -s 'echo configured-accept'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;5;8m configured-accept\x1b[0m")
        write(fd, b"\x02\n")
        output = read_until(fd, b"configured-accept")
        if b"echo configured-accept" not in output:
            raise AssertionError(f"configured accept keyseq did not accept: {output!r}")
        if PROMPT not in output:
            read_until(fd, PROMPT)
        reset_session(fd)

        run(fd, "BASH_AUTOSUGGEST_EXECUTE_KEYSEQS='\\C-b'")
        run(fd, "bash_autosuggestions bind")
        run(fd, "history -s 'echo configured-execute'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;5;8m configured-execute\x1b[0m")
        write(fd, b"\x02")
        output = read_until(fd, b"configured-execute")
        if b"echo configured-execute" not in output:
            raise AssertionError(f"configured execute keyseq did not execute: {output!r}")
        if PROMPT not in output:
            read_until(fd, PROMPT)
        reset_session(fd)

        run(fd, "BASH_AUTOSUGGEST_CLEAR_KEYSEQS='\\C-g'")
        run(fd, "bash_autosuggestions bind")
        run(fd, "history -s 'echo configured-clear'")
        write(fd, b"echo")
        read_until(fd, b"\x1b[38;5;8m configured-clear\x1b[0m")
        write(fd, b"\x07")
        cleared = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m configured-clear\x1b[0m" in cleared:
            raise AssertionError(f"configured clear keyseq did not clear suggestion: {cleared!r}")
        reset_session(fd)

        run(fd, "BASH_AUTOSUGGEST_PARTIAL_ACCEPT_KEYSEQS='\\C-t'")
        run(fd, "bash_autosuggestions bind")
        run(fd, "history -s 'git checkout main'")
        write(fd, b"git ")
        read_until(fd, b"\x1b[38;5;8mcheckout main\x1b[0m")
        write(fd, b"\x14")
        read_until(fd, b"\x1b[38;5;8m main\x1b[0m")
        reset_session(fd)

        write(fd, b"echo foo")
        write(fd, b"\x15")
        write(fd, b"echo bar")
        write(fd, b"\x15")
        write(fd, b"\x19")
        write(fd, b"\x1by")
        write(fd, b"\n")
        output = read_until(fd, b"foo")
        if b"foo" not in output:
            raise AssertionError(f"yank-pop did not cycle to first kill: {output!r}")
        if PROMPT not in output:
            read_until(fd, PROMPT)
        write(fd, b"echo foo")
        write(fd, b"\x15")
        write(fd, b"echo bar")
        write(fd, b"\x15")
        write(fd, b"\x19")
        write(fd, b"\x1by")
        write(fd, b"\x1by")
        write(fd, b"\n")
        output = read_until(fd, b"bar")
        if b"bar" not in output:
            raise AssertionError(f"yank-pop did not cycle back to second kill: {output!r}")
        if PROMPT not in output:
            read_until(fd, PROMPT)

        run(fd, "bind 'set enable-bracketed-paste on'")
        run(fd, "history -s 'echo foo'")
        write(fd, b"echo ")
        read_until(fd, b"\x1b[38;5;8mfoo\x1b[0m")
        write(fd, b"\x1b[200~bar\x1b[201~")
        pasted = read_until(fd, b"bar")
        if b"\x1b[38;5;8mfoo\x1b[0m" in pasted:
            raise AssertionError(f"bracketed paste retained old suggestion: {pasted!r}")
        clear_line(fd)

        run(fd, "bind '\"\\C-b\": autosuggest-disable'")
        run(fd, "history -s 'echo hello'")
        write(fd, b"\x02")
        write(fd, b"\x1b[200~echo aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\x1b[201~")
        disabled_paste = read_available(fd, timeout=0.5)
        if b"\x1b[38;5;8m" in disabled_paste:
            raise AssertionError(f"disabled bracketed paste showed suggestion: {disabled_paste!r}")
        reset_session(fd)

        run(fd, "history -s $'echo \"\\n\"'")
        write(fd, b"e")
        multiline = read_until(fd, b'cho "\r\n"', timeout=5.0)
        if b"\x1b[38;5;8mcho \"" not in multiline:
            raise AssertionError(f"multi-line suggestion did not use highlight style: {multiline!r}")
        if b"\x1b[s" not in multiline or b"\x1b[u" not in multiline:
            raise AssertionError(f"multi-line suggestion did not preserve cursor: {multiline!r}")
        reset_session(fd)

        set_pty_size(fd, 8, 24)
        run(fd, "history -s 'echo wrap-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'")
        write(fd, b"echo ")
        wrapped = read_until(fd, b"wrap-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", timeout=5.0)
        saved, before_restore, after_restore, max_row = cursor_after_wrapped_suggestion(
            wrapped,
            cols=24,
            start_col=len(PROMPT),
        )
        if saved is None or before_restore is None or after_restore is None:
            raise AssertionError(f"wrapped suggestion did not save and restore cursor: {wrapped!r}")
        if max_row <= saved[0]:
            raise AssertionError(f"wrapped suggestion did not cross terminal width: {wrapped!r}")
        if after_restore != saved:
            raise AssertionError(
                f"wrapped suggestion restored cursor to {after_restore}, expected {saved}: {wrapped!r}"
            )
        set_pty_size(fd, 24, 80)
        reset_session(fd)

        run(fd, "set -o vi")
        run(fd, "history -s 'foobar foo'")
        write(fd, b"foo")
        read_until(fd, b"\x1b[38;5;8mbar foo\x1b[0m")
        write(fd, b"\x1bh")
        read_until(fd, b"\x1b[38;5;8mbar foo\x1b[0m")
        clear_line(fd)

        run(fd, "history -c")
        run(fd, "history -s 'foobar foo'")
        write(fd, b"foo")
        read_until(fd, b"\x1b[38;5;8mbar foo\x1b[0m")
        write(fd, b"\x1bea")
        write(fd, b"baz")
        read_until(fd, b"foobarbaz")
        clear_line(fd)

        run(fd, "history -c")
        run(fd, "history -s 'foobar foo'")
        write(fd, b"foo")
        read_until(fd, b"\x1b[38;5;8mbar foo\x1b[0m")
        write(fd, b"\x1bwa")
        write(fd, b"az")
        read_until(fd, b"foobar faz")
        clear_line(fd)

        run(fd, "history -c")
        run(fd, "history -s 'foobar foo'")
        write(fd, b"foo")
        read_until(fd, b"\x1b[38;5;8mbar foo\x1b[0m")
        write(fd, b"\x1bf")
        write(fd, b"oa")
        write(fd, b"b")
        read_until(fd, b"foobar fob")
        clear_line(fd)

        write(fd, b"echo foo")
        write(fd, b"\x1bdlaX")
        write(fd, b"\n")
        output = read_until(fd, b"foX")
        if b"fooX" in output:
            raise AssertionError(f"vi-delete did not remove the last character: {output!r}")
        if PROMPT not in output:
            read_until(fd, PROMPT)

        write(fd, b"exit\n")
        os.waitpid(pid, 0)
    finally:
        try:
            os.close(fd)
        except OSError:
            pass
        shutil.rmtree(tmpdir)


if __name__ == "__main__":
    main()
