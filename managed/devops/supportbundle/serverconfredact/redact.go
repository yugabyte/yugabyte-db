// Copyright (c) YugabyteDB, Inc.

package serverconfredact

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"regexp"
	"sort"
	"strings"
)

// SecretReplacement is the token every redactor substitutes for a secret. support_bundle_utils.sh
// also greps for it to confirm redaction actually happened.
const SecretReplacement = "REDACTED"

// Field names always redacted, regardless of the caller's gflag list.
var defaultPatterns = []string{
	"ldapbindpasswd",
	"radiussecrets",
	"ldapBindPassword",
	"ycql_ldap_bind_passwd",
	"ldapServiceAccountPassword",
}

// RedactSensitiveInfo redacts LDAP passwords and version-specific sensitive gflags in server.conf
// text. sensitiveGflags may be nil.
//
// ysql_hba_conf_csv is excluded from the wholesale pass on purpose: it holds the entire pg_hba
// ACL, so replacing the value outright would destroy configuration the bundle exists to show.
// Only its ldapbindpasswd and radiussecrets values are sensitive, and hba_conf.go and ldap_hba.go
// redact those in place instead.
//
// Patterns are sorted before use so that output does not depend on map iteration order. The Java
// iterates a HashSet here and is order-dependent; sorting is a deliberate improvement, and is
// safe because the redactors are applied to disjoint field names.
func RedactSensitiveInfo(input string, sensitiveGflags []string) string {
	if input == "" {
		return input
	}
	patterns := make(map[string]struct{})
	for _, p := range defaultPatterns {
		patterns[p] = struct{}{}
	}
	for _, g := range sensitiveGflags {
		if g == "" || g == "ysql_hba_conf_csv" {
			continue
		}
		patterns[g] = struct{}{}
	}
	ordered := make([]string, 0, len(patterns))
	for p := range patterns {
		ordered = append(ordered, p)
	}
	sort.Strings(ordered)

	output := input
	for _, pattern := range ordered {
		if !isHbaAttribute(pattern) {
			output = redactFlagLines(output, pattern)
		}
		output = redactPatternOccurrences(pattern, output)
	}
	return output
}

// isHbaAttribute reports whether name is a pg_hba attribute, redacted inside ysql_hba_conf_csv
// rather than as a flag.
func isHbaAttribute(name string) bool {
	_, ok := findHbaSecret(name)
	return ok || name == hbaKey
}

// redactFlagLines redacts the value of each --<name>= flag that starts a line, up to the end of the
// line: gflags reads a flagfile one flag per line and takes the rest of it as the value, so
// "abc --def" and "abc,def=ghi" are each one secret. A value quoted up to the end of the line is
// left to the key=value redactor, which keeps the quotes.
func redactFlagLines(input, name string) string {
	locs := flagLinePattern(name).FindAllStringIndex(input, -1)
	if locs == nil {
		return input
	}
	var b strings.Builder
	from := 0
	for _, loc := range locs {
		vs, ve := loc[1], lineEnd(input, loc[1])
		if quotedValueEnd(input, vs) == ve {
			continue
		}
		b.WriteString(input[from:vs])
		b.WriteString(SecretReplacement)
		from = ve
	}
	b.WriteString(input[from:])
	return b.String()
}

// flagLinePattern matches "--<name>=" at the start of a line, leading blanks allowed.
func flagLinePattern(name string) *regexp.Regexp {
	return regexp.MustCompile(`(?m)^[ \t]*--` + regexp.QuoteMeta(name) + `=`)
}

// lineEnd returns the end of the line holding i.
func lineEnd(s string, i int) int {
	if j := strings.IndexAny(s[i:], "\r\n"); j >= 0 {
		return i + j
	}
	return len(s)
}

// quotedValueEnd is where the key=value redactor ends a "…" or \"…\" value starting at i, or -1
// when the value isn't quoted.
func quotedValueEnd(s string, i int) int {
	switch {
	case strings.HasPrefix(s[i:], `\"`):
		return scanPastEscapedCSVQuotedValue(s, i+2)
	case strings.HasPrefix(s[i:], `"`):
		return scanPastCSVQuotedValue(s, i+1)
	}
	return -1
}

// redactPatternOccurrences routes one field name through every applicable redactor.
//
// ldapbindpasswd is redacted as YBA redacts it (HbaConfRedactor.redactFreeText). The hbaSecrets
// attributes take a different path: the HBA parser runs first because it is the only redactor that
// preserves the surrounding quoting style, and only the JSON-key forms ("radiussecrets": ...) can
// still remain afterwards. Running the key=value, CLI and Python-dict redactors on them as well
// would corrupt the hba quoting the parser just preserved.
func redactPatternOccurrences(pattern, output string) string {
	if pattern == hbaKey {
		return redactLdapBindPasswdText(output)
	}
	if secret, ok := findHbaSecret(pattern); ok {
		output = RedactHbaSecretValues(output, secret, SecretReplacement)
		if !contains(output, pattern) {
			return output
		}
		output = redactStandardJSONQuotedStringValue(output, pattern, SecretReplacement)
		output = redactBackslashEscapedJSONQuotedKeyValue(output, pattern, SecretReplacement)
		output = redactJSONBareValue(output, pattern, SecretReplacement)
		return output
	}
	if !contains(output, pattern) {
		return output
	}
	output = redactStandardJSONQuotedStringValue(output, pattern, SecretReplacement)
	output = redactJSONBareValue(output, pattern, SecretReplacement)
	output = redactBackslashEscapedJSONQuotedKeyValue(output, pattern, SecretReplacement)
	output = redactKeyEqualsValue(output, pattern, SecretReplacement)
	output = redactCliFlagValue(output, pattern, SecretReplacement)
	output = redactPythonDictValue(output, pattern, SecretReplacement)
	return output
}

// contains keeps the empty-substring guard: strings.Contains reports true for "", where the
// redactors rely on false so an empty pattern short-circuits rather than matching everywhere.
func contains(s, sub string) bool {
	return sub != "" && strings.Contains(s, sub)
}

// indexOf is strings.Index with a start offset. It replaced a naive O(n*m) double loop that ran
// once per pattern per redactor, rescanning the whole file dozens of times.
func indexOf(s, sub string, from int) int {
	if from < 0 {
		from = 0
	}
	if from > len(s) {
		return -1
	}
	i := strings.Index(s[from:], sub)
	if i < 0 {
		return -1
	}
	return i + from
}

// ParseSensitiveGflagsB64 decodes a base64 JSON array of gflag names.
func ParseSensitiveGflagsB64(b64 string) ([]string, error) {
	if b64 == "" {
		return nil, nil
	}
	raw, err := decodeBase64String(b64)
	if err != nil {
		return nil, err
	}
	var flags []string
	if err := json.Unmarshal([]byte(raw), &flags); err != nil {
		return nil, err
	}
	return flags, nil
}

// ParseSensitiveGflagsCSV parses a comma-separated gflag list.
func ParseSensitiveGflagsCSV(csv string) []string {
	if csv == "" {
		return nil
	}
	parts := strings.Split(csv, ",")
	out := make([]string, 0, len(parts))
	for _, p := range parts {
		p = trimSpace(p)
		if p != "" {
			out = append(out, p)
		}
	}
	return out
}

// trimSpace uses an explicit cutset rather than strings.TrimSpace, which additionally strips \v,
// \f and Unicode spaces. Gflag names cannot contain those, but preserving the exact cutset keeps
// this swap behaviour-identical.
func trimSpace(s string) string {
	return strings.Trim(s, " \t\n\r")
}

func isWhitespace(c byte) bool {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r'
}

func isFlagNameStart(c byte) bool {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'
}

func isFlagNameChar(c byte) bool {
	return isFlagNameStart(c) || (c >= '0' && c <= '9')
}

// ErrUnredacted marks a VerifyRedacted failure: a secret is still readable.
var ErrUnredacted = errors.New("left unredacted")

// WriteIfRedacted writes redacted to path only if VerifyRedacted passes. Otherwise the file keeps
// its unredacted content, which the node then deletes. Mode 0 is only safe because path always
// names an existing file: WriteFile truncates it and keeps its permissions.
func WriteIfRedacted(path, redacted string, sensitiveGflags []string) error {
	if err := VerifyRedacted(redacted, sensitiveGflags); err != nil {
		return err
	}
	return os.WriteFile(path, []byte(redacted), 0)
}

// VerifyRedacted checks RedactSensitiveInfo's output: ldapbindpasswd as verifyLdapBindPasswd
// describes, every radiussecrets value must start with REDACTED, and the whole rest of the line
// after each --<sensitive>= flag that starts one must read REDACTED, quotes aside; an empty flag
// value passes. The caller must not write the file when it fails, so a gap in the parsers costs the
// node its copy of server.conf rather than the secret. The exception is where an ldapbindpasswd
// value ends: that check reads it with the redactors' parsers, and without them checks only that
// the value starts with REDACTED. The error names the field and line, never the value.
func VerifyRedacted(output string, sensitiveGflags []string) error {
	if err := verifyLdapBindPasswd(output); err != nil {
		return err
	}
	for _, secret := range hbaSecrets {
		field := secret.name + "="
		for pos := indexOf(output, field, 0); pos >= 0; pos = indexOf(output, field, pos+1) {
			v := pos + len(field)
			for v < len(output) && (output[v] == '"' || output[v] == '\'' || output[v] == '\\') {
				v++
			}
			if v >= len(output) || isWhitespace(output[v]) ||
				strings.IndexByte(",}]", output[v]) >= 0 {
				continue
			}
			if !strings.HasPrefix(output[v:], SecretReplacement) {
				line := lineOf(output, pos)
				return fmt.Errorf("%s %w on line %d", secret.name, ErrUnredacted, line)
			}
		}
	}
	for _, name := range flagNames(sensitiveGflags) {
		for _, loc := range flagLinePattern(name).FindAllStringIndex(output, -1) {
			value := strings.Trim(output[loc[1]:lineEnd(output, loc[1])], `"'\`)
			if value != "" && value != SecretReplacement {
				return fmt.Errorf("--%s %w on line %d", name, ErrUnredacted, lineOf(output, loc[0]))
			}
		}
	}
	return nil
}

// flagNames is every name RedactSensitiveInfo treats as a flag: the defaults and the given list,
// less the hba attributes and ysql_hba_conf_csv (redacted inside, not wholesale).
func flagNames(sensitiveGflags []string) []string {
	var names []string
	for _, name := range append(append([]string{}, defaultPatterns...), sensitiveGflags...) {
		if name != "" && !isHbaAttribute(name) && name != "ysql_hba_conf_csv" {
			names = append(names, name)
		}
	}
	return names
}

func lineOf(s string, pos int) int {
	return strings.Count(s[:pos], "\n") + 1
}
