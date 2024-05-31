#!/bin/sh
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations under
# the License.


die() {
  echo "$@"
  exit 1
}

find_or_die() {
  which "$1" >/dev/null 2>&1 || die "$1 not found!"
}

[ "$#" -eq 1 ] || die "Syntax: $0 <pid>"

pid="${1}"

OS="$(uname)"
case $OS in
  'Linux')
    find_or_die "gdb"
    printf "set auto-load safe-path / \n attach %s \n thread apply all bt \n" "$pid" | gdb
    ;;
  'Darwin')
    find_or_die "lldb"
    lldb --one-line "bt all" --one-line "exit" -p "$pid"
    ;;
  *)
    die "Unsupported OS!"
    ;;
esac
