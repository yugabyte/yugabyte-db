#! /usr/bin/env bash

#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
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
