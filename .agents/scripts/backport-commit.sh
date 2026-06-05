#!/usr/bin/env bash
# Thin wrapper: exec backport-commit.py with the same args. Keeps the
# `.agents/scripts/backport-commit.sh` invocation path stable so the
# /backport-commit slash command's allowed-tools rule matches without
# change. The real implementation lives in backport-commit.py.
exec python3 "$(dirname "${BASH_SOURCE[0]}")/backport-commit.py" "$@"
