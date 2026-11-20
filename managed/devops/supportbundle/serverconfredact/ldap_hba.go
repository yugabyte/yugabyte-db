// Copyright (c) YugabyteDB, Inc.

package serverconfredact

import "strings"

// hbaSecret is a pg_hba attribute that holds a secret.
type hbaSecret struct {
	name string
	// list values are comma lists (radiussecrets="s1,s2"), so a comma never ends one.
	list bool
}

// hbaSecrets are redacted inside ysql_hba_conf_csv, which keeps the rest of the ACL. ldapbindpasswd
// is not one: hba_conf.go redacts it as YBA does. YBA doesn't redact radiussecrets.
var hbaSecrets = []hbaSecret{
	{name: "radiussecrets", list: true},
}

func findHbaSecret(name string) (hbaSecret, bool) {
	for _, secret := range hbaSecrets {
		if secret.name == name {
			return secret, true
		}
	}
	return hbaSecret{}, false
}

// quoteStyle is the quoting a parsed hba secret value used in the input. It is recorded so
// that the replacement can be re-encoded identically — see encodeLDAP. Preserving the style
// matters because ysql_hba_conf_csv remains a working pg_hba ACL after redaction; emitting the
// wrong quoting would corrupt the surrounding record.
//
// Three escape depths exist because the value reaches us through a variable number of
// serialisations: the gflag itself is CSV-quoted, embedding it in JSON escapes the quotes once,
// and logging that JSON as a shell string escapes them again. For a secret pw:
//
//	quoteUnquoted               radiussecrets=pw
//	quoteSingleCSV              radiussecrets="pw"
//	quoteDoubledCSV             radiussecrets=""pw""
//	quoteEscapedDoubledCSV      radiussecrets=\"\"pw\"\"
//	quoteDeepEscapedDoubledCSV  radiussecrets=\\"\\"pw\\"\\"
type quoteStyle int

const (
	quoteUnquoted quoteStyle = iota
	quoteDoubledCSV
	quoteEscapedDoubledCSV
	quoteDeepEscapedDoubledCSV
	quoteSingleCSV
)

// parsedValue is one decoded hba secret occurrence: where it ends in the input, its plaintext,
// and the quoting to re-encode the replacement with.
type parsedValue struct {
	endExclusive int
	decoded      string
	style        quoteStyle
}

// RedactHbaSecretValues redacts every value of one hbaSecrets attribute in a ysql_hba-style string,
// re-encoding the replacement in each value's original quoting style.
func RedactHbaSecretValues(input string, secret hbaSecret, replacement string) string {
	if input == "" {
		return input
	}
	key := secret.name + "="
	out := make([]byte, 0, len(input))
	last := 0
	for {
		idx := indexOf(input, key, last)
		if idx < 0 {
			out = append(out, input[last:]...)
			break
		}
		out = append(out, input[last:idx]...)
		eq := idx + len(key)
		parsed := tryParseHbaValue(input, eq, secret)
		if parsed == nil {
			out = append(out, key...)
			last = eq
			continue
		}
		out = append(out, key...)
		out = append(out, encodeLDAP(parsed.style, replacement)...)
		last = parsed.endExclusive
	}
	return string(out)
}

// encodeLDAP renders a replacement in the given quoting style, escaping any interior quote to the
// same depth as the wrapper so the result round-trips through tryParseHbaValue.
func encodeLDAP(style quoteStyle, decodedPassword string) string {
	if decodedPassword == "" {
		return ""
	}
	switch style {
	case quoteUnquoted:
		return decodedPassword
	case quoteEscapedDoubledCSV:
		body := strings.ReplaceAll(decodedPassword, `"`, `\"\"`)
		return `\"\"` + body + `\"\"`
	case quoteDeepEscapedDoubledCSV:
		body := strings.ReplaceAll(decodedPassword, `"`, `\\"\\"`)
		return `\\"\\"` + body + `\\"\\"`
	case quoteDoubledCSV:
		body := strings.ReplaceAll(decodedPassword, `"`, `""`)
		return `""` + body + `""`
	case quoteSingleCSV:
		return `"` + strings.ReplaceAll(decodedPassword, `"`, `""`) + `"`
	default:
		return decodedPassword
	}
}

// isClosingDelimiterFollowedByTerminator reports whether what follows a candidate closing
// delimiter actually terminates the field.
//
// The name= check is the non-obvious rule: pg_hba attributes are space-separated name=value pairs,
// so the next token being any <name>= (ldapbasedn=, clientcert=, map=, ...) is what proves the
// current value ended. Without it a password containing a delimiter-like sequence would swallow
// the following attributes, and a value followed by an option it didn't know would not be
// redacted at all.
func isClosingDelimiterFollowedByTerminator(s string, indexAfterDelimiter int, list bool) bool {
	j := indexAfterDelimiter
	for j < len(s) && isWhitespace(s[j]) {
		j++
	}
	if j >= len(s) {
		return true
	}
	switch s[j] {
	case ',':
		return !list
	case '}', ']':
		return true
	case '"':
		if j+1 < len(s) && s[j+1] == '"' {
			return false
		}
		return true
	}
	// pg_hba separates attributes with whitespace, so a name= right against the quote is still
	// part of the value.
	if j == indexAfterDelimiter || !isFlagNameStart(s[j]) {
		return false
	}
	k := j + 1
	for k < len(s) && isFlagNameChar(s[k]) {
		k++
	}
	return k < len(s) && s[k] == '='
}

// scanDoubledCSVInner scans the body of a ""-quoted value and returns the offset of the closing
// "", or -1. Interior "" pairs decode to a single literal quote.
func scanDoubledCSVInner(s string, i int, list bool, decoded *strings.Builder) int {
	for i < len(s) {
		if i+1 < len(s) && s[i] == '"' && s[i+1] == '"' {
			if isClosingDelimiterFollowedByTerminator(s, i+2, list) {
				return i
			}
			decoded.WriteByte('"')
			i += 2
		} else {
			decoded.WriteByte(s[i])
			i++
		}
	}
	return -1
}

// scanSingleCSVInner scans the body of a "-quoted value, decoding "" as a literal quote, and
// returns the offset just past the closing quote.
func scanSingleCSVInner(s string, i int, decoded *strings.Builder) int {
	for i < len(s) {
		if s[i] == '"' {
			if i+1 < len(s) && s[i+1] == '"' {
				decoded.WriteByte('"')
				i += 2
			} else {
				return i + 1
			}
		} else {
			decoded.WriteByte(s[i])
			i++
		}
	}
	return i
}

// isEscapedDoubledOpen matches the 4-byte \"\" wrapper.
func isEscapedDoubledOpen(s string, i int) bool {
	return i+4 <= len(s) && s[i] == '\\' && s[i+1] == '"' && s[i+2] == '\\' && s[i+3] == '"'
}

// isDeepEscapedDoubledOpen matches the 6-byte \\"\\" wrapper.
func isDeepEscapedDoubledOpen(s string, i int) bool {
	return i+6 <= len(s) && s[i] == '\\' && s[i+1] == '\\' && s[i+2] == '"' &&
		s[i+3] == '\\' && s[i+4] == '\\' && s[i+5] == '"'
}

// scanEscapedWrapperInner is scanDoubledCSVInner for the two escaped depths; wrapperLen is 4 for
// \"\" and 6 for \\"\\".
func scanEscapedWrapperInner(s string, i, wrapperLen int, list bool, decoded *strings.Builder) int {
	for i < len(s) {
		if (wrapperLen == 4 && isEscapedDoubledOpen(s, i)) ||
			(wrapperLen == 6 && isDeepEscapedDoubledOpen(s, i)) {
			if isClosingDelimiterFollowedByTerminator(s, i+wrapperLen, list) {
				return i
			}
			decoded.WriteByte('"')
			i += wrapperLen
		} else {
			decoded.WriteByte(s[i])
			i++
		}
	}
	return -1
}

// tryParseHbaValue identifies which quoting style the value at eqIndex uses and decodes it,
// returning nil when no style parses cleanly (the caller then leaves the text untouched).
//
// Order matters: the deepest escaping is tested first, because \\"\\" also starts with the
// bytes that would match shallower forms and would otherwise be mis-parsed.
func tryParseHbaValue(s string, eqIndex int, secret hbaSecret) *parsedValue {
	if eqIndex > len(s) {
		return nil
	}
	if isDeepEscapedDoubledOpen(s, eqIndex) {
		decoded := &strings.Builder{}
		closeStart := scanEscapedWrapperInner(s, eqIndex+6, 6, secret.list, decoded)
		if closeStart >= 0 && isDeepEscapedDoubledOpen(s, closeStart) {
			return &parsedValue{closeStart + 6, decoded.String(), quoteDeepEscapedDoubledCSV}
		}
		return nil
	}
	if isEscapedDoubledOpen(s, eqIndex) {
		decoded := &strings.Builder{}
		closeStart := scanEscapedWrapperInner(s, eqIndex+4, 4, secret.list, decoded)
		if closeStart >= 0 && isEscapedDoubledOpen(s, closeStart) {
			return &parsedValue{closeStart + 4, decoded.String(), quoteEscapedDoubledCSV}
		}
		return nil
	}
	if eqIndex+2 <= len(s) && s[eqIndex:eqIndex+2] == `""` {
		decoded := &strings.Builder{}
		closeStart := scanDoubledCSVInner(s, eqIndex+2, secret.list, decoded)
		if closeStart >= 0 && closeStart+2 <= len(s) && s[closeStart:closeStart+2] == `""` {
			return &parsedValue{closeStart + 2, decoded.String(), quoteDoubledCSV}
		}
		return nil
	}
	if eqIndex < len(s) && s[eqIndex] == '"' {
		decoded := &strings.Builder{}
		end := scanSingleCSVInner(s, eqIndex+1, decoded)
		if end > eqIndex+1 {
			return &parsedValue{end, decoded.String(), quoteSingleCSV}
		}
		return nil
	}
	// Only a literal '"' is excluded here. A backslash must NOT be excluded: every well-formed
	// backslash form is already claimed by the escaped-doubled branches above, so a value still
	// starting with one, such as the single-escaped radiussecrets=\"pw\", has no quote style and
	// is redacted here as unquoted rather than left in place.
	if eqIndex < len(s) && s[eqIndex] != '"' {
		end, val := scanUnquotedHbaValue(s, eqIndex)
		if end > eqIndex {
			return &parsedValue{end, val, quoteUnquoted}
		}
	}
	return nil
}

// scanUnquotedHbaValue returns the end offset and text of an unquoted hba secret value.
//
// An unquoted value ends before the next whitespace-separated <name>= attribute, or at end of line.
// Because the value itself may contain spaces, the scanner cannot simply stop at whitespace; it
// must look ahead past the whitespace and check whether an attribute name followed by '=' actually
// begins there.
//
// Go's regexp is RE2 and supports no lookaround, hence a hand-written scanner. The lookahead regex
// of the former Java LdapBindPasswdHbaFormat, run with github.com/dlclark/regexp2, was measured
// against it and is less safe: `.` does not cross \n, so p\nnext=1 left the value in the file, and
// a,OU=x was cut to "a" without the k-j>=3 rule below.
func scanUnquotedHbaValue(s string, start int) (int, string) {
	i := start
	n := len(s)
	for i < n {
		if s[i] == '\n' || s[i] == '\r' {
			return i, s[start:i]
		}
		if isWhitespace(s[i]) {
			j := i + 1
			for j < n && isWhitespace(s[j]) {
				j++
			}
			if attributeAt(s, j) {
				return i, s[start:i]
			}
		}
		if i > start && attributeAt(s, i) {
			k := i
			for k > start && isWhitespace(s[k-1]) {
				k--
			}
			if k == start || isWhitespace(s[k-1]) {
				return k, s[start:i]
			}
		}
		if s[i] == ',' {
			j := i + 1
			for j < n && isWhitespace(s[j]) {
				j++
			}
			if j < n && isFlagNameStart(s[j]) {
				k := j + 1
				for k < n && isFlagNameChar(s[k]) {
					k++
				}
				if k-j >= 3 && k < n && s[k] == '=' {
					return i, s[start:i]
				}
			}
			if j+5 <= n {
				rest := s[j:]
				if len(rest) >= 5 && (rest[:5] == "local" || rest[:4] == "host") {
					return i, s[start:i]
				}
			}
		}
		i++
	}
	if start < n {
		return n, s[start:n]
	}
	return start, ""
}

// attributeAt reports whether a <name>= begins at j.
func attributeAt(s string, j int) bool {
	if j >= len(s) || !isFlagNameStart(s[j]) {
		return false
	}
	k := j + 1
	for k < len(s) && isFlagNameChar(s[k]) {
		k++
	}
	return k < len(s) && s[k] == '='
}
