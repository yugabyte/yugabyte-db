// Copyright (c) YugabyteDB, Inc.

// Package serverconfredact removes credentials from a DB node's server.conf in place, so secrets
// never leave the node in the clear when a support bundle is collected.
//
// server.conf is a gflags file, but secrets sit inside values that may be truncated JSON from a
// log line, shell- or CSV-quoted, or escaped several levels deep, so one secret has ~13 quoting
// shapes. Each redactor is a byte-preserving splicer: it finds the secret's byte span, replaces only
// those bytes, and emits the rest unchanged, since support diffs the file against the live node.
// That rules out encoding/json (reorders and reformats), strconv.Unquote/QuotedPrefix (Go syntax;
// under-redacts `""abc""`), text/scanner, line splitting (values contain newlines) and RE2 (no
// lookahead; see ldap_hba.go).
//
// This is a port of RedactingService.java, with names and branch order mirrored, and of
// HbaConfRedactor.java (hba_conf.go), which redacts ldapbindpasswd by parsing ysql_hba_conf_csv as
// Postgres does; hba_conf_test.go checks that the port's output matches YBA's byte for byte. It
// differs where RedactingService leaked: a --<sensitive>= flag runs to the end of the line (gflags
// reads one flag per line), and the hba attribute radiussecrets is redacted in place
// (ldap_hba.go). VerifyRedacted then checks the output before anything is written.
//
// Contract with support_bundle_utils.sh: exit 0 redacted, 1 I/O or decode failure, 2 usage error,
// 3 a value was left unredacted (the file is not written); `smoke` prints the replacement token. On
// any non-zero exit the node drops its copy of server.conf and reports REDACTION_FAILURE; YBA does
// not redact.
package serverconfredact
