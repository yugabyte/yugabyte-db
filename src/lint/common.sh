#!/usr/bin/env bash
#
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# Common variables/functions for linters.

export LC_ALL=C

check_ctags() {
  if ! which ctags >/dev/null || \
     ! grep -q "Exuberant Ctags" <<<"$(ctags --version)"; then
    echo "Please install Exuberant Ctags" >/dev/stderr
    if which dnf >/dev/null; then
      echo "HINT: dnf install ctags" >/dev/stderr
    elif which brew >/dev/null; then
      echo "HINT: brew install ctags" >/dev/stderr
    elif which apt >/dev/null; then
      echo "HINT: apt install exuberant-ctags" >/dev/stderr
    fi
    return 1
  fi
}
