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
# Simple linter to make sure a file is uniquely sorted.
set -u

. "${BASH_SOURCE%/*}/common.sh"

lineno=$(diff "$1" <(sort -u "$1") | head -1 | grep -Eo '^[0-9]+')
if [ -n "$lineno" ]; then
  echo 'error:file_not_unique_sorted:This file should be uniquely sorted:'\
"$lineno:$(sed -n "$lineno"p "$1")"
fi
