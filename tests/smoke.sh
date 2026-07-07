#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

bash --noprofile --norc -ic '
  enable -f ./bash-autosuggestions.so bash_autosuggestions
  bash_autosuggestions enable
  bash_autosuggestions status
  bash_autosuggestions disable
' </dev/null

python3 tests/pty_smoke.py
