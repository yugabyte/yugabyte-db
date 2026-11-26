// Copyright (c) YugabyteDB, Inc.

// Package logparse decides which log files fall inside a support bundle's collection window. It
// runs on a DB node as yb-log-parse, matching filenames against YBA-supplied regexes and
// normalising dates, once per candidate file.
//
// It uses github.com/dlclark/regexp2, not regexp: YBA's patterns use negative lookahead such as
// (?!.*WARNING), which RE2 rejects. Backtracking is acceptable because the patterns come from YBA
// configuration, not from the node.
//
// Contract with support_bundle_utils.sh, which reads stdout with $(...) and falls back to inline
// Python on a non-zero exit: output has no trailing newline (match-batch prints one line per
// input), and the pre-flight probe `add-days 20260101 0` must print exactly "20260101".
package logparse
