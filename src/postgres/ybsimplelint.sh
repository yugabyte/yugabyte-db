#!/usr/bin/env bash
# Simple linter for postgres code.
grep -nE '\s+$' "$1" | sed 's/^/error:trailing_whitespace:/'
grep -nvE '^(	* {0,3}\S|$)' "$1" | sed 's/^/error:leading_whitespace:/'
grep -nE '(\)|else|else if)\s+{$' "$1" | sed 's/^/warning:likely_bad_brace:/'

grep -nE ',\s*errmsg(_plural)?\(' "$1" | sed 's/^/error:missing_linebreak_before_errmsg:/'
grep -nE ',\s*errdetail(_plural)?\(' "$1" | sed 's/^/error:missing_linebreak_before_errdetail:/'
grep -nE ',\s*errhint\(' "$1" | sed 's/^/error:missing_linebreak_before_errhint:/'

grep -nE 'errmsg(_plural)?\("[A-Z][a-z]' "$1" | sed 's/^/warning:likely_bad_capitalization_in_errmsg:/'
grep -nE 'errdetail(_plural)?\("[a-z]' "$1" | sed 's/^/warning:likely_bad_lowercase_in_errdetail:/'
grep -nE 'errhint\("[a-z]' "$1" | sed 's/^/warning:likely_bad_lowercase_in_errhint:/'
