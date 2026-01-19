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
# Simple linter for postgres code.
set -u

. "${BASH_SOURCE%/*}/common.sh"

is_yb_file() {
  if [ $# != 1 ]; then
    echo "Invalid arguments $*" >&2
    exit 1
  fi
  if [[ "$1" =~ /yb[^/]+\.[ch]$ ||
        "$1" =~ /pg_yb[^/]+\.[ch]$ ||
        "$1" =~ /nodeYb[^/]+\.[ch]$ ||
        "$1" =~ /yb-extensions/ ]]; then
    return 0
  else
    return 1
  fi
}

# Includes
if is_yb_file "$1"; then
  if grep -iqE '(yb|yugabyte) includes' "$1"; then
    echo 'error:bad_yb_includes_block:'\
'YB files should not have a YB includes block:1:'"$(head -1 "$1")"
  fi

  # Includes should be organized into empty-line-separated blocks.  The blocks
  # should appear in order and not twice.  Each block should be sorted.
  #
  # Order of blocks:
  # 0: c.h, postgres.h, or postgres_fe.h
  # 1: <stdlib.h>
  # 2: regular.h
  pattern0='^#include "(c|postgres(_fe)?).h"'
  pattern1='^#include <.*\.h(pp)?>'
  pattern2='^#include ".*\.h"'
  pattern_ignore='^#(if|else|endif)'
  block_type=
  prev_block_type=
  prev_line=
  while read -r line; do
    # Split to lineno and line contents.
    # shellcheck disable=SC2001 # parameter expansion doesn't support char set
    lineno=$(sed 's/[:-].*//' <<<"$line")
    # shellcheck disable=SC2001 # parameter expansion doesn't support char set
    line=$(sed -E 's/[0-9]+[:-]//' <<<"$line")

    # Empty lines indicate the boundary between blocks.
    if [ -z "$line" ]; then
      prev_block_type=$block_type
      block_type=
      continue
    fi

    if [[ "$line" =~ $pattern0 ]]; then
      line_type=0
    elif [[ "$line" =~ $pattern1 ]]; then
      line_type=1
    elif [[ "$line" =~ $pattern2 ]]; then
      line_type=2
    elif [[ "$line" =~ $pattern_ignore ]]; then
      continue
    else
      echo 'error:unidentified_includes_line:'\
"Could not determine include line type:$lineno:$line"
      continue
    fi

    if [ -z "$block_type" ]; then
      if [ -n "$prev_block_type" ] && \
         [ "$prev_block_type" -ge "$line_type" ]; then
        echo 'error:bad_includes_block:'\
"Duplicate or misordered includes block:$lineno:$line"
      fi
      block_type=$line_type
    else
      if [ "$line_type" -ne "$block_type" ]; then
        echo 'error:bad_line_in_includes_block:'\
"Includes block has line of mismatching type:$lineno:$line"
      elif [ "$block_type" -eq 0 ]; then
        echo 'error:including_both_c_and_postgres:'\
"Should only include one of c.h, postgres.h, or postgres_fe.h:$lineno:$line"
      fi
      if [[ ! "$prev_line" < "$line" ]]; then
        echo 'error:bad_sort_in_includes_block:'\
"Includes block is not unique sorted:$lineno:$line"
      fi
    fi

    prev_line=$line
  done < <(grep -nB 100 '^#include' "$1" | grep -EA 100 '^[0-9]+[:-]#include')
else
  diff_result=$("${BASH_SOURCE%/*}"/diff_file_with_upstream.py "$1")
  exit_code=$?
  if [ $exit_code -ne 0 ]; then
    if [ $exit_code -ne 2 ]; then
      # The following messages are not emitted to stderr because those messages
      # may be buried under a large python stacktrace also emitted to stderr.
      if [ -z "$diff_result" ]; then
        echo "Unexpected failure, exit code $exit_code"
      else
        echo "$diff_result"
      fi
      exit 1
    fi

    echo 'error:bad_filename_for_yb_file:'\
'This is a YB-introduced file, but the linter does not recognize it as such.'\
' If it is YB-introduced, rename the file better (such as with "yb" prefix).'\
' If the file is from an upstream repository, update'\
' upstream_repositories.csv. The corresponding commit in'\
' upstream_repositories.csv should exist either locally in ~/code/<repo_name>'\
' or remotely in the corresponding remote repository (and you need internet'\
' access in that case).:1:'"$(head -1)"
  else
    grep -inE '(yb|yugabyte) includes' "$1" \
      | grep -vE '^[0-9]+:/\* YB includes \*/$' \
      | while read -r line; do
          echo 'error:bad_yb_includes_comment:'\
'YB includes comment should be formatted exactly like /* YB includes */:'"$line"
      done

    upstream_commit_message_suffix=', or if importing an upstream commit,'\
' upstream_repositories.csv should be updated correspondingly'

    # Find upstream-side hunks that match "#include...".
    # It's hard to pinpoint which YB line is relevant to the lack of upstream
    # line, so just take the first line of each hunk.
    grep -E '^(> #include|[0-9])' <<<"$diff_result" \
      | grep -B1 '^>' \
      | grep -Eo '^[0-9]+' \
      | while read -r hunk_start_lineno; do
          echo 'error:upstream_include_missing:'\
'An upstream include in this area is missing'\
"$upstream_commit_message_suffix":\
"$hunk_start_lineno:$(sed -n "$hunk_start_lineno"p "$1")"
        done

    # Find YB-side hunks that match "/* YB includes */" or "#include...".
    grep -E '^(< #include|/\* YB includes \*/$|[0-9])' <<<"$diff_result" \
      | grep -B1 '^<' \
      | grep -Eo '^[0-9]+(,[0-9]+)?' \
      | while read -r line_ranges; do
          hit_yb_includes_block=false
          expect_newline=false
          hit_last_line=false
          prev_line=
          while read -r line; do
            # Split to lineno and line contents.
            # shellcheck disable=SC2001 # parameter expansion doesn't support
            # char set
            lineno=$(sed 's/[:-].*//' <<<"$line")
            # shellcheck disable=SC2001 # parameter expansion doesn't support
            # char set
            line=$(sed -E 's/[0-9]+[:-]//' <<<"$line")

            if "$hit_last_line"; then
              if [[ "$line" == '#include '* ]]; then
                echo 'error:include_not_in_yb_includes_block:'\
'YB-added includes should be put in a YB includes block,'\
' and such blocks should not have empty lines in them:'"$lineno:$line"
              fi
              continue
            fi

            c_include="#include \"c.h\""
            yb_c_include="$c_include"$'\t\t\t\t\t'"/* YB include */"
            pg_include_re="#include \"postgres(_fe)?\\.h\""
            yb_pg_include_re="$pg_include_re"$'\t\t\t'"+/\\* YB include \\*/"
            if ! "$hit_yb_includes_block"; then
              if [[ "$line" == "$c_include"* ]]; then
                if [[ "$line" != "$yb_c_include" ]]; then
                  echo 'error:c_include_missing_yb_comment:'\
'YB-added c.h includes should have YB include comment inlined'\
"$upstream_commit_message_suffix:$lineno:$line"
                elif [[ "$(grep -n '^#include' "$1" \
                           | head -1)" != "$lineno:$line" ]]; then
                  echo 'error:bad_include_c:'\
'YB-added c.h should be the first include, or if there exists an'\
' upstream-owned postgres.h or postgres_fe.h include, then YB need not add a'\
' c.h include:'"$lineno:$line"
                fi
                expect_newline=true
                continue
              elif [[ "$line" =~ $pg_include_re ]]; then
                if [[ ! "$line" =~ $yb_pg_include_re ]]; then
                  echo 'error:postgres_include_missing_yb_include_comment:'\
'YB-added postgres.h or postgres_fe.h includes should have YB include comment'\
' inlined'"$upstream_commit_message_suffix:$lineno:$line"
                elif [[ "$(grep -n '^#include' "$1" \
                             | head -1)" != "$lineno:$line" ]]; then
                  if [[ "$(grep -n '^#include' "$1" \
                             | head -2 )" != "$((lineno - 1)):#include \"c.h\"\n$lineno:$line" ]]; then
                    echo 'error:bad_include_postgres:'\
'YB-added postgres.h or postgres_fe.h should be the first include, or if'\
' there exists an upstream-owned c.h include, then it should be the second'\
' include:'"$lineno:$line"
                  fi
                else
                  expect_newline=true
                fi
                continue
              elif "$expect_newline"; then
                if [ -n "$line" ]; then
                  echo 'error:c_or_postgres_include_not_followed_by_newline:'\
'YB-added c.h, postgres.h, or postgres_fe.h where upstream has no such'\
' includes should be followed by a newline:'"$lineno:$line"
                fi
                expect_newline=false
                continue
              elif [[ "$line" != '/* YB includes */' ]]; then
                echo 'error:include_not_in_yb_includes_block:'\
'YB-added includes should be put in a YB includes block'\
"$upstream_commit_message_suffix:$lineno:$line"
                break
              fi
              hit_yb_includes_block=true
              continue
            fi

            if [ -z "$line" ]; then
              hit_last_line=true
            elif [[ "$line" != '#include '* ]]; then
              if [[ "$line" =~ ^#(if|else|endif) ]]; then
                continue
              fi
              echo 'error:non_include_in_yb_includes_block:'\
"YB includes block has a non-include line:$lineno:$line"
            elif [[ ! "$prev_line" < "$line" ]]; then
              echo 'error:bad_sort_in_includes_block:'\
"Includes block is not unique sorted:$lineno:$line"
            elif [[ "$line" == "$c_include"* ]] || \
                 [[ "$line" =~ $pg_include_re ]]; then
              echo 'error:c_or_postgres_is_not_first_include:'\
'YB-added c.h, postgres.h, and postgres_fe.h do not belong in a YB includes'\
' block. Rather, they should be the first includes with YB include comment'\
' inlined.:'"$lineno:$line"
            fi

            prev_line=$line
          done < <(grep -n '' "$1" | sed -n "$line_ranges"p)
        done

    # For kwlist.h, new keywords by YB should have "_YB_" prefix.
    #
    # TODO(jason): this does not belong in the "includes" section, but it is
    # also wasteful to re-run diff_file_with_upstream.py a second time.  Should
    # refactor this in the future.
    if [[ "$1" == */kwlist.h ]]; then
      grep -E '^< ' <<<"$diff_result" \
        | grep -v ', _YB_' \
        | sed 's/^< //' \
        | while read -r line; do
            grep -nF "$line" "$1" \
              | sed 's/^/error:yb_keyword_missing_yb_prefix:'\
'YB-added keywords should have "_YB_" prefix and "_P" suffix:/'
          done
    fi
  fi

  # This time, run with --ignore-space-change.
  diff_result=$("${BASH_SOURCE%/*}"/diff_file_with_upstream.py "$1" \
                --ignore-space-change)
  exit_code=$?
  if [ $exit_code -ne 0 ]; then
    if [ $exit_code -eq 2 ]; then
      echo "Unexpected exit code 2"
    fi
    # The following messages are not emitted to stderr because those messages
    # may be buried under a large python stacktrace also emitted to stderr.
    if [ -z "$diff_result" ]; then
      echo "Unexpected failure, exit code $exit_code"
    else
      echo "$diff_result"
    fi
    exit 1
  fi

  # Find YB-side hunks.
  while read -r line_ranges; do
    if sed -n "$line_ranges"p "$1" \
         | grep -Eq 'YB|Yb|yb|YSQL|Ysql|ysql|Yuga|yuga'; then
      continue
    fi
    lineno=${line_ranges%%,*}
    echo 'warning:missing_yb_marker_for_yb_changes:'\
'YB changes to upstream owned files should have "yb", "ysql", or "yuga".'\
' In case the YB changes are legitimate, prioritize resolving this by'\
' renaming YB-introduced variables/functions/types with YB prefix; if there'\
' are no such objects, add a YB comment. If the YB changes are not legitimate'\
' (for example, uncalled for whitespace changes), revert the changes back to'\
' the way they look like in upstream. This lint rule expects YB markers on a'\
' hunk basis, not line basis. To see the hunks, try running'\
' src/lint/diff_file_with_upstream.py '"$1"' -b, and find hunks with lines'\
' starting with <:'\
"$lineno:$(sed -n "$lineno"p "$1")"
  done < <(grep -E '^(< |[0-9])' <<<"$diff_result" \
           | grep -B1 '^<' \
           | grep -Eo '^[0-9]+(,[0-9]+)?')
fi

if [[ "$1" == src/postgres/third-party-extensions/* ]]; then
  # Remaining rules are not intended for third-party-extensions.
  exit
fi

# Naming
if [[ "$1" =~ /[^/]+yb[^/]+\.[ch]$ &&
      ! "$1" =~ /pg_yb[^/]+\.[ch]$ &&
      "$1" != */pg_verifybackup.c ]]; then
  echo 'error:bad_yb_nonprefix_filename:'\
'Filenames with "yb" should generally start with "yb":1:'"$(head -1 "$1")"
fi
if [[ "$1" =~ /ybc[^/]+\.[ch]$ &&
      "$1" != */ybctid.h ]]; then
  echo 'error:bad_ybc_prefix_filename:'\
'Filenames with "ybc" prefix do not belong in src/postgres:1:'"$(head -1 "$1")"
fi
if [[ "$1" =~ /[^/]*Yb[^/]+\.[ch]$ &&
      ! "$1" =~ /nodeYb[^/]+\.[ch]$ ]]; then
  echo 'error:bad_Yb_filename:'\
'Filenames with "Yb" should only be the case for nodeYb* files:1:'"$(head -1 "$1")"
fi
check_ctags
echo "$1" \
  | ctags -n -L - --languages=c,c++ --c-kinds=t --c++-kinds=t -f /dev/stdout \
  | while read -r line; do
      symbol=$(echo "$line" | cut -f1)
      lineno=$(echo "$line" | cut -f3 | grep -Eo '^[0-9]+')

      if [[ "$symbol" == YBC* ||
            "$symbol" == Ybc* ||
            "$symbol" == ybc* ]]; then
        echo 'error:bad_ybc_prefix:This type should not have "ybc" prefix:'\
"$lineno:$(sed -n "$lineno"p "$1")"
      fi

      # Ideally, we want to catch all YB-added types to make sure they have
      # "yb", but it is not possible to determine which are YB-added or not.
      # So as a best effort, at least we know YB files contain only YB code, so
      # whatever types they produce should have "yb".
      if is_yb_file "$1" &&
         [[ "$symbol" != *YB* &&
            "$symbol" != *Yb* &&
            "$symbol" != *yb* ]]; then
        echo 'error:missing_yb_prefix:This type should have "yb" prefix:'\
"$lineno:$(sed -n "$lineno"p "$1")"
      fi
    done

while read -r exclude_re; do
  if [[ "$1" =~ $exclude_re ]]; then
    # Remaining rules do not apply to this file since it is pgindent exempt.
    exit
  fi
done < <(grep -v --no-filename \
           '^#' src/postgres/src/tools/pgindent/*exclude_file_patterns)

# Whitespace
grep -nE '\s+$' "$1" \
  | sed 's/^/error:trailing_whitespace:Remove trailing whitespace:/'
grep -nvE '^('$'\t''* {0,3}\S|$)' "$1" \
  | sed 's/^/error:leading_whitespace:'\
'Use tabs followed by 0-3 spaces for leading whitespace:/'

# there are three cases to catch:
# 1. /*no whitespace before/after*/
# 2. /*no whitespace before */
# 3. /* no whitespace after*/
# We search for strings that do not start or end with a whitespace: \S+(.*\S+)?
# leading / trailing whitespace is not caught by that regex, so we can check it
# explicitly.
#
# second grep removes lines that have continuous ---, *** characters.
# third grep removes /*#define or /*- lines because PG uses those sometimes.
# third grep also removes /*+ lines because pg_hint_plan hints are like that.
if ! [[ "$1" == src/postgres/src/backend/utils/activity/pgstat.c ||
        "$1" == src/postgres/contrib/pgcrypto/px-crypt.h ||
        "$1" == src/postgres/src/include/tsearch/dicts/regis.h ]]; then
  grep -nE '/\*(\S+(.*\S+)?|\s\S+(.*\S+)?|\S+(.*\S+)?\s)\*/' "$1" \
    | grep -vE '[\*\-]{3}' \
    | grep -vE '/\*(#define|[+-])' \
    | sed 's,^,error:bad_comment_spacing:'\
'Spacing should be like /* param */:,'
fi

if ! [[ "$1" == src/postgres/contrib/ltree/* ||
        "$1" == src/postgres/src/backend/utils/adt/tsquery.c ]]; then
  grep -nE '^\s*(if|else if|for|while)\(' "$1" \
    | grep -vE 'while\((0|1)\)' \
    | sed 's,^,error:bad_spacing_after_if_else_for_while:'\
'There should be a space after if/for/while:,'
fi
# fn(arg1 /* bad */,
#    arg2 /* bad */);
# fn(arg1,\t/* good */
#    arg2);\t/* good */
# fn(arg1 /* acceptable */ ,
#    arg2 /* acceptable */ );
# pg_dump has some comments in strings, hence the allowance of '"'.
if ! [[ "$1" == src/postgres/src/interfaces/ecpg/preproc/output.c ]]; then
  grep -nE '\s\*/' "$1" \
    | grep -vE '\s\*/([\"[:space:]]|$)' \
    | sed 's/^/error:bad_spacing_after_comment:'\
'Comment should generally be followed by space or EOL:/'
fi
# fn(/* bad */ arg1,
#    arg2);
if ! [[ "$1" == src/postgres/src/interfaces/ecpg/preproc/output.c ]]; then
  grep -nE '/\*\s' "$1" \
    | grep -vE '([\"[:space:]]|^[0-9]+:)/\*\s' \
    | sed 's/^/error:bad_spacing_before_comment:'\
'Comment should generally be preceded by space or beginning of line:/'
fi

# Comments
# The second grep excludes // comments if the first non-space character of the
# line is a '*', to allow most cases where // comments are inside a /* */ block
if ! [[ "$1" == src/postgres/src/common/d2s.c ||
        "$1" == src/postgres/src/common/d2s_intrinsics.h ]]; then
  grep -nE '//\s' "$1" \
    | grep -vE '^[0-9]+:\s+\*\s' \
    | sed 's|^|error:bad_comment_style:Use /* comment */, not // comment:|'
fi
# /* this is a bad
#  * multiline comment */
# TupleTableSlot slot /* this is a good
#                      * inline comment */
# /**************
#  * this is fine
#  */
# /*-------------
#  * this is fine
#  */
# /* TypeCategory()
#  * this is fine
#  */
# /*\t\tbox_same
#  * this is fine
#  */
grep -nE '^\s*/\*[^/]*[^)*-/]$' "$1" \
  | grep -vE '/\*'$'\t\t''\w' \
  | sed 's,^,warning:likely_bad_multiline_comment_start:'\
'Start multiline comments with empty "/*":,'
# /*
# * this is a bad
# * multiline comment
# */
grep -nE '(^|^\s*'$'\t'')\*/' "$1" \
  | sed 's/^/warning:likely_bad_multiline_comment_end:'\
'Multiline comments should align all "*"s:/'

# Pointers
#
# Second grep below is to exclude comments:
# - lines starting with comments
# - \w\* inside a single-line or inline comment.
grep -nE '\w\*+(\s|\)|$)' "$1" \
  | grep -vE '^[0-9]+:\s*/?\*\s|.*/\*.*\w\*.*\*/' \
  | sed 's/^/warning:likely_bad_pointer:'\
'Pointer asterisks should be after whitespace:/'

# Functions
#
# Second grep below is to exclude function declarations (or catch function
# declarations missing storage class).  Unfortunately, it is not easy to
# distinguish static function definitions from declarations when the parameter
# list spans multiple lines, so those cases are missed.
grep -nE '^\w+(\s+\w+)+\(' "$1" \
  | grep -vE '^[0-9]+:(NON_EXEC_STATIC|extern|static)\s.*[^)]$' \
  | sed 's/^/warning:likely_bad_function_signature:'\
'In case this is a function declaration, this is missing "extern" or "static"'\
' storage class; in case this is a function definition, the return type'\
' should be on a previous line:/'

# Variables
#
# Variable declarations should have the variable name aligned at 12 columns, or
# no alignment and just one space separating the variable type and name.  The
# first command excludes such no-alignment cases; the second command finds bad
# alignment.  '(' is needed for cases such as
#
#     void\t\t(*startup_fn) (Node *clause, PredIterInfo info);
#
# Note that this excludes macros and assembly code (identified by lines ending
# with '\') because pgindent doesn't appear to properly format those and
# upstream postgres code has no consistent style (see
# src/postgres/src/include/access/valid.h, for example).
grep -nE '^\s+\w+(\s\s+|'$'\t'')[_[:alpha:]*(]' "$1" \
  | perl -ne 'print unless /\\$/ || /^\d+:\s+'\
'(\w{1}(\t\t| {7})'\
'|\w{2}(\t\t| {6})'\
'|\w{3}(\t\t| {5})'\
'|\w{4}(\t| {4})'\
'|\w{5}(\t| {3})'\
'|\w{6}(\t| {2})'\
'|\w{7}(\t| {1})'\
'|\w{8})'\
'((?<=\t)(\t| {3}\*| {2}\*\*| \*\*\*)'\
'|(?<= )( {4}| {3}\*| {2}\*\*| \*\*\*)'\
'|(?<=\w)'\
'(\w{0}(\t| {4}| {3}\*| {2}\*\*)'\
'|\w{1}(\t| {3}| {2}\*)'\
'|\w{2}(\t| {2})'\
'|\w{3}\t))'\
'[\w(]/' \
  | sed 's/^/error:bad_variable_declaration_spacing:'\
'Variable declarations should align variable names to the 12 column mark:/'

# Braces
#
# For curly braces, ignore code comments and macros as they may deviate from
# the expected style.
#
# The first grep below is immune to macros.  The second grep below filters out
# multiline comments.
grep -nE '(\)|else)\s+{$' "$1" \
  | grep -vE '^[0-9]+:\s+\*' \
  | sed 's,^,error:bad_opening_brace:'\
'Brace should not be on the same line as if/else:,'
# Find macros as lines ending in backslash.  If the else is the last line of
# the macro, there is no backslash there, so look for a backslash in the
# previous line (hence the -B1 and -A1 dance below).  The last grep below
# filters out multiline comments.
grep -nEB1 '}\s+else(.*[^\])?$' "$1" \
  | grep -EA1 '^[0-9]+-.*[^\]$' \
  | grep -E '}\s+else' \
  | grep -vE '^[0-9]+:\s+\*' \
  | sed 's/^/error:bad_closing_brace:'\
'Brace should not be on the same line as else:/'
if ! [[ "$1" == src/postgres/contrib/bloom/bloom.h ||
        "$1" == src/postgres/src/include/replication/reorderbuffer.h ||
        "$1" == src/postgres/src/timezone/zic.c ]]; then
  # - Exclude cases where ( is followed by a line starting with '#' (for #ifdef,
  #   #ifndef, etc.)
  # - Exclude comments
  grep -nA1 '($' "$1" \
    | vi -ens +'g/^\d\+-#/.-1,.d' +'%write! /dev/stdout' +'q' /dev/stdin \
    | grep '($' \
    | grep -Ev '^[0-9]+:\s*\*\s' \
    | grep -Ev '__asm__\s__volatile__\(' \
    | sed 's/^/error:bad_opening_paren:There should be no linebreak after (:/'
fi
grep -nE '^extern "C" {' "$1" \
  | sed 's/^/error:bad_opening_brace_extern:'\
'Brace should not be on the same line as extern "C":/'

# Logging
if ! [[ "$1" == src/postgres/src/backend/utils/activity/pgstat_function.c ||
        "$1" == src/postgres/src/include/postmaster/startup.h ]]; then
  grep -nE ',\s*err(code|((msg|detail|hint)(_plural)?))\([^)]' "$1" \
    | sed 's/^/error:missing_linebreak_before_err:'\
'err(code|msg|detail|hint) should be on its own line:/'
fi
grep -nE ',\s*\(err(code|((msg|detail|hint)(_plural)?))\([^)].*[^;]$' "$1" \
  | sed 's/^/warning:missing_linebreak_before_paren_err:'\
'err(code|msg|detail|hint) should be on its own line:/'
# The first grep misses cases such as
#     ereport((somecondition ? ERROR : WARNING),
# but at the time of writing, those cases don't have the issue this lint
# warning is trying to catch.
# Alternatively, if \w+ is substituted with .*, it would throw additional
# errors on cases already caught by the above linebreak rules.
grep -nEA1 '^\s*ereport\(\w+,$' "$1" \
  | grep -E '^[0-9]+-\s+err(code|((msg|detail|hint)(_plural)?))\(' \
  | sed -E 's/^([0-9]+)-/\1:/' \
  | sed 's/^/warning:missing_paren_before_err:'\
'err(code|msg|detail|hint) should be preceded by (:/'
grep -nE '[([:space:]]errmsg(_plural)?\("[A-Z][-'"'"'a-z]*\s' "$1" \
  | sed 's/^/warning:likely_bad_capitalization_in_errmsg:'\
'errmsg should generally not capitalize the first word:/'
grep -nE '[([:space:]]errdetail(_plural)?\("[-'"'"'a-z]+[:[:space:]]' "$1" \
  | sed 's/^/warning:likely_bad_lowercase_in_errdetail:'\
'errdetail should generally capitalize the first word:/'
grep -nE '[([:space:]]errhint(_plural)?\("[-'"'"'a-z]+[:[:space:]]' "$1" \
  | sed 's/^/warning:likely_bad_lowercase_in_errhint:'\
'errhint should generally capitalize the first word:/'
