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

# Whitespace
if ! [[ "$1" == src/postgres/src/backend/snowball/libstemmer/* ||
        "$1" == src/postgres/src/interfaces/ecpg/test/expected/* ]]; then
  grep -nE '\s+$' "$1" \
    | sed 's/^/error:trailing_whitespace:/'
fi
if ! [[ "$1" == src/postgres/src/backend/snowball/libstemmer/* ||
        "$1" == src/postgres/src/interfaces/ecpg/test/expected/* ||
        "$1" == src/postgres/src/include/snowball/libstemmer/* ||
        "$1" == src/postgres/src/pl/plperl/ppport.h ]]; then
  grep -nvE '^(	* {0,3}\S|$)' "$1" \
    | sed 's/^/error:leading_whitespace:/'
fi
grep -nE '/\*(\w+|\s\w+|\w+\s)\*/' "$1" \
  | sed 's/^/error:bad_parameter_comment_spacing:/'
if ! [[ "$1" == src/postgres/contrib/ltree/* ||
        "$1" == src/postgres/src/backend/snowball/libstemmer/* ||
        "$1" == src/postgres/src/backend/utils/adt/tsquery.c ||
        "$1" == src/postgres/src/interfaces/ecpg/test/expected/* ||
        "$1" == src/postgres/src/interfaces/ecpg/test/thread/* ||
        "$1" == src/postgres/src/pl/plperl/ppport.h ]]; then
  grep -nE '^\s*(if|else if|for|while)\(' "$1" \
    | grep -vE 'while\((0|1)\)' \
    | sed 's/^/error:bad_spacing_after_if_else_for_while:/'
fi
# fn(arg1 /* bad */,
#    arg2 /* bad */);
# fn(arg1,	/* good */
#    arg2);	/* good */
# fn(arg1 /* acceptable */ ,
#    arg2 /* acceptable */ );
# TODO(jason): make this an error after running pgindent in the future.
if ! [[ "$1" == src/postgres/src/interfaces/ecpg/preproc/output.c ]]; then
  grep -nE '\s\*/' "$1" \
    | grep -vE '\s\*/([\"[:space:]]|$)' \
    | sed 's/^/warning:bad_spacing_after_comment:/'
fi
# fn(/* bad */ arg1,
#    arg2);
if ! [[ "$1" == src/postgres/src/include/snowball/libstemmer/header.h ||
        "$1" == src/postgres/src/interfaces/ecpg/preproc/output.c ||
        "$1" == src/postgres/src/interfaces/ecpg/preproc/preproc.c ||
        "$1" == src/postgres/src/interfaces/ecpg/test/* ]]; then
  grep -nE '/\*\s' "$1" \
    | grep -vE '([\"[:space:]]|^\S+)/\*\s' \
    | sed 's/^/error:bad_spacing_before_comment:/'
fi

# Comments
grep -nE '//\s' "$1" \
  | sed 's/^/error:bad_comment_style:/'
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
# /*		box_same
#  * this is fine
#  */
grep -nE '^\s*/\*[^/]*[^)*-/]$' "$1" \
  | grep -vE '/\*		\w' \
  | sed 's/^/warning:likely_bad_multiline_comment_start:/'
# /*
# * this is a bad
# * multiline comment
# */
grep -nE '(^|^\s*	)\*/' "$1" \
  | sed 's/^/warning:likely_bad_multiline_comment_end:/'

# Pointers
#
# Second grep below is to exclude comments:
# - lines starting with comments
# - \w\* inside a single-line or inline comment.
grep -nE '\w\*+(\s|\)|$)' "$1" \
  | grep -vE '^[0-9]+:\s*/?\*\s|.*/\*.*\w\*.*\*/' \
  | sed 's/^/warning:likely_bad_pointer:/'

# Functions
#
# Second grep below is to exclude function declarations (or catch function
# declarations missing storage class).  Unfortunately, it is not easy to
# distinguish static function definitions from declarations when the parameter
# list spans multiple lines, so those cases are missed.
grep -nE '^\w+(\s+\w+)+\(' "$1" \
  | grep -vE '^[0-9]+:(NON_EXEC_STATIC|extern|static)\s.*[^)]$' \
  | sed 's/^/warning:likely_bad_function_signature:/'

# Variables
#
# Variable declarations should have the variable name aligned at 12 columns, or
# no alignment and just one space separating the variable type and name.  The
# first command excludes such no-alignment cases; the second command finds bad
# alignment.  '(' is needed for cases such as
#
#     void		(*startup_fn) (Node *clause, PredIterInfo info);
grep -nE '^\s+\w+(\s\s+|	)[_[:alpha:]*(]' "$1" \
  | perl -ne 'print unless /^\d+:\s+'\
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
  | sed 's/^/error:bad_variable_declaration_spacing:/'

# Braces
grep -nE '(\)|else)\s+{$' "$1" \
  | sed 's/^/warning:likely_bad_opening_brace:/'
grep -nE '}\s+else' "$1" \
  | sed 's/^/warning:likely_bad_closing_brace:/'
if ! [[ "$1" == src/postgres/contrib/bloom/bloom.h ||
        "$1" == src/postgres/src/include/replication/reorderbuffer.h ||
        "$1" == src/postgres/src/pl/plperl/ppport.h ||
        "$1" == src/postgres/src/timezone/zic.c ]]; then
  # - Exclude cases where ( is followed by a line starting with '#' (for #ifdef,
  #   #ifndef, etc.)
  # - Exclude comments
  grep -nA1 '($' "$1" \
    | vi -es +'g/^\d\+-#/.-1,.d' +'%write! /dev/stdout' +'q' /dev/stdin \
    | grep '($' \
    | grep -Ev '^[0-9]+:\s*\*\s' \
    | grep -Ev '__asm__\s__volatile__\(' \
    | sed 's/^/error:bad_opening_paren:/'
fi

# Logging
if ! [[ "$1" == src/postgres/src/backend/utils/activity/pgstat_function.c ||
        "$1" == src/postgres/src/include/postmaster/startup.h ]]; then
  grep -nE ',\s*err(code|((msg|detail|hint)(_plural)?))\([^)]' "$1" \
    | sed 's/^/error:missing_linebreak_before_err:/'
fi
grep -nE ',\s*\(err(code|((msg|detail|hint)(_plural)?))\([^)].*[^;]$' "$1" \
  | sed 's/^/warning:missing_linebreak_before_paren_err:/'
# The first grep misses cases such as
#     ereport((somecondition ? ERROR : WARNING),
# but at the time of writing, those cases don't have the issue this lint
# warning is trying to catch.
# Alternatively, if \w+ is substituted with .*, it would throw additional
# errors on cases already caught by the above linebreak rules.
grep -nEA1 '^\s*ereport\(\w+,$' "$1" \
  | grep -E '^[0-9]+-\s+err(code|((msg|detail|hint)(_plural)?))\(' \
  | sed -E 's/^([0-9]+)-/\1:/' \
  | sed 's/^/warning:missing_paren_before_err:/'
grep -nE '[([:space:]]errmsg(_plural)?\("[A-Z][-'"'"'a-z]*\s' "$1" \
  | sed 's/^/warning:likely_bad_capitalization_in_errmsg:/'
grep -nE '[([:space:]]errdetail(_plural)?\("[-'"'"'a-z]+[:[:space:]]' "$1" \
  | sed 's/^/warning:likely_bad_lowercase_in_errdetail:/'
grep -nE '[([:space:]]errhint(_plural)?\("[-'"'"'a-z]+[:[:space:]]' "$1" \
  | sed 's/^/warning:likely_bad_lowercase_in_errhint:/'
