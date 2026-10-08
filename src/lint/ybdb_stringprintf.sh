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
# Linter that rejects the StringPrintf family in src/yb code.
set -euo pipefail

. "${BASH_SOURCE%/*}/common.sh"

# yb/gutil/stringprintf.h stays for gutil's own use, so nothing but this rule
# keeps the printf-style helpers out of new code.  Lines that start a // or /*
# comment, or continue a block comment with "* ", are skipped so that prose can
# still name them; "*out = StringPrintf(...)" is still code.  ybc_util.cc
# formats a va_list passed from C callers, which Format() cannot take, so
# StringAppendV is allowed there.
pattern='StringPrintf|SStringPrintf|StringAppendF|StringPrintfVector'
if [[ "$1" != src/yb/yql/pggate/util/ybc_util.cc ]]; then
  pattern+='|StringAppendV'
fi
{ grep -nwE "$pattern" "$1" || true; } \
  | { grep -vE '^[0-9]+:\s*(//|/\*|\*(\s|/|$))' || true; } \
  | sed 's|^|error:bad_stringprintf:'\
'Use Format() instead of the StringPrintf family. For printf-style field'\
' formatting pass FixedPoint, ZeroPadded, HexString, PadLeft, PadRight or'\
' Scientific from yb/util/format.h as an argument:|'
