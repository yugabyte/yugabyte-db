#! /usr/bin/env bash

set -euo pipefail

prog=$(basename "$0")

if [ "$#" -gt 0 ]; then
  if [ "$1" == "--install" ]; then
    echo "Manually installing pre-commit hook"
    if [ -e ".git/hooks/pre-commit" ]; then
      echo "You already have a pre-commit hook install. Will not overwrite..." >&2
      exit 1
    fi

    ln -s "../../arcanist_util/$prog" ".git/hooks/pre-commit"
    exit $?
  else
    echo "Usage: $prog [--install]" >&2
    exit 1
  fi
fi

commit_hash=$(git merge-base master HEAD)

(
  # get keyboard control as --patch is interactive
  exec < /dev/tty
  set +e
  git-clang-format --commit $commit_hash --patch
  ret=$?
  set -e

  if [ "$ret" -gt 0 ]; then
    echo "Error in running git-clang-format. Probably missing binary... " \
         "Either fix the problem, or commit with --no-verify" >&2
    exit $ret
  fi
)
