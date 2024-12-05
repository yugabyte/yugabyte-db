#!/usr/bin/env bash
# Simple linter for postgres code.
grep -nE '\s+$' "$1" | sed 's/^/error:trailing_whitespace:/'
grep -nvE '^(	* {0,3}\S|$)' "$1" | sed 's/^/error:leading_whitespace:/'

# Second grep below is to exclude comments:
# - lines starting with comments
# - \w\* inside a single-line or inline comment.
grep -nE '\w\*+(\s|\)|$)' "$1" | grep -vE '^[0-9]+:\s*/?\*\s|.*/\*.*\w\*.*\*/' | sed 's/^/warning:likely_bad_pointer:/'

grep -nE '/\*(\w+|\s\w+|\w+\s)\*/' "$1" | sed 's/^/error:bad_parameter_comment_spacing:/'

# Second grep below is to exclude function declarations (or catch function
# declarations missing storage class).  Unfortunately, it is not easy to
# distinguish static function definitions from declarations when the parameter
# list spans multiple lines, so those cases are missed.
grep -nE '^\w+(\s+\w+)+\(' "$1" | grep -vE '^[0-9]+:(NON_EXEC_STATIC|extern|static)\s.*[^\)]$' | sed 's/^/warning:likely_bad_function_signature:/'

grep -nE '(\)|else)\s+{$' "$1" | sed 's/^/warning:likely_bad_opening_brace:/'
grep -nE '}\s+else' "$1" | sed 's/^/warning:likely_bad_closing_brace:/'

grep -nE ',\s*errmsg(_plural)?\(' "$1" | sed 's/^/error:missing_linebreak_before_errmsg:/'
grep -nE ',\s*errdetail(_plural)?\(' "$1" | sed 's/^/error:missing_linebreak_before_errdetail:/'
grep -nE ',\s*errhint\(' "$1" | sed 's/^/error:missing_linebreak_before_errhint:/'

grep -nE 'errmsg(_plural)?\("[A-Z][a-z]' "$1" | sed 's/^/warning:likely_bad_capitalization_in_errmsg:/'
grep -nE 'errdetail(_plural)?\("[a-z]' "$1" | sed 's/^/warning:likely_bad_lowercase_in_errdetail:/'
grep -nE 'errhint\("[a-z]' "$1" | sed 's/^/warning:likely_bad_lowercase_in_errhint:/'
