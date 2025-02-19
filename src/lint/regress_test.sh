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
# Simple linter for regress test.
set -u

. "${BASH_SOURCE%/*}/util.sh"

if ! [[ "$1" =~ /yb[^/]+$ ]]; then
  echo "Unexpected file $1" >&2
  exit 1
fi

if ! [[ "$1" =~ (expected/[^/]+\.out|specs/[^/]+\.spec|sql/[^/]+\.sql)$ ]]; then
  echo 'error:bad_regress_test_file_extension:'\
"${1##*/} has unexpected extension:1:$(head -1 "$1")"
fi

if [[ "$1" =~ /yb.port.[^/]+$ ]]; then
  # Remove "yb.port." prefix.
  pg_orig_test=${1/yb.port./}
  # Remove alternative expectfile suffix.
  pg_orig_test=$(sed -E 's/_[0-9].out/.out/' <<<"$pg_orig_test")
  if ! [ -f "$pg_orig_test" ]; then
    echo 'error:original_regress_test_missing:'\
"Regress test $pg_orig_test does not exist:1:$(head -1 "$1")"
  fi
elif ! [[ "$1" =~ /yb.(depd|orig).[^/]+$ ]]; then
  echo 'error:bad_regress_test_file_prefix:'\
"${1##*/}"' has "yb" prefix but does not fit into any known category among '\
'"yb.depd.", "yb.orig.", or "yb.port.":1:'"$(head -1 "$1")"
fi
