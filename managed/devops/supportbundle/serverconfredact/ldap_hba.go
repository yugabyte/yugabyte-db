// Copyright (c) YugabyteDB, Inc.

package serverconfredact

import "strings"

// hbaSecret is a pg_hba attribute that holds a secret.
type hbaSecret struct {
	name string
	// nextPrefix is the attribute-name prefix whose <name>= ends an unquoted value.
	nextPrefix string
	// list values are comma lists (radiussecrets="s1,s2"), so a comma never ends one.
	list bool
}

// hbaSecrets are redacted inside ysql_hba_conf_csv, which keeps the rest of the ACL. ldapbindpasswd
// follows the Java's rules; Java has no radiussecrets.
var hbaSecrets = []hbaSecret{
	{name: "ldapbindpasswd", nextPrefix: "ldap"},
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
// and logging that JSON as a shell string escapes them again. For a password pw:
//
//	quoteUnquoted               ldapbindpasswd=pw
//	quoteSingleCSV              ldapbindpasswd="pw"
//	quoteDoubledCSV             ldapbindpasswd=""pw""
//	quoteEscapedDoubledCSV      ldapbindpasswd=\"\"pw\"\"
//	quoteDeepEscapedDoubledCSV  ldapbindpasswd=\\"\\"pw\\"\\"
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
	// Only a literal '"' is excluded here, matching the negative lookahead in the former Java
	// LdapBindPasswdHbaFormat.UNQUOTED_VALUE. A backslash must NOT be excluded: every well-formed
	// backslash form is already claimed by the escaped-doubled branches above, so a value still
	// starting with one is a shape neither side has a quote style for -- such as the single-escaped
	// ldapbindpasswd=\"pw\" -- and Java redacts it here as unquoted. Excluding backslash left that
	// form untouched on the node while YBA redacted it, and because the node then reported
	// REDACTION_STATUS=NODE, YBA skipped its own pass and the password shipped.
	if eqIndex < len(s) && s[eqIndex] != '"' {
		end, val := scanUnquotedHbaValue(s, eqIndex, secret.nextPrefix)
		if end > eqIndex {
			return &parsedValue{end, val, quoteUnquoted}
		}
	}
	return nil
}

// scanUnquotedHbaValue returns the end offset and text of an unquoted hba secret value.
//
// An unquoted value ends before the next whitespace-separated attribute whose name starts with
// nextPrefix (ldap*= for ldapbindpasswd, any <name>= for radiussecrets), or at end of line. Because
// the password itself may contain spaces, the scanner cannot simply stop at whitespace; it must
// look ahead past the whitespace and check whether such an attribute name followed by '=' actually
// begins there.
//
// The Java used a lookahead regex for this (LdapBindPasswdHbaFormat.UNQUOTED_VALUE). Go's regexp
// is RE2 and supports no lookaround, hence a hand-written scanner. github.com/dlclark/regexp2
// could run the Java pattern verbatim, but it was measured against this scanner and must not be
// swapped in: the pattern is strictly less safe here.
//
//	input                        scanner                regexp2 with the Java pattern
//	ldapbindpasswd=p\nnext=1     redacts "p"            NO MATCH -> password left in the file
//	ldapbindpasswd=               value ""               no match
//	ldapbindpasswd=a,OU=x         redacts "a,OU=x"       redacts only "a", leaks ",OU=x"
//
// The newline case is an under-redaction: `.` does not cross \n. The DN case is the k-j>=3 rule,
// which lives in Java's findUnquotedValueEnd but NOT in UNQUOTED_VALUE, so this scanner is more
// conservative than the Java regex by design.
//
// Those rows are also live Go/Java divergences: the same server.conf redacted on the node (here)
// and on the YBA host (Java) differs on those three inputs.
func scanUnquotedHbaValue(s string, start int, nextPrefix string) (int, string) {
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
			if attributeAt(s, j, nextPrefix) {
				return i, s[start:i]
			}
		}
		if i > start && attributeAt(s, i, nextPrefix) {
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

// attributeAt reports whether a <name>= whose name starts with prefix begins at j.
func attributeAt(s string, j int, prefix string) bool {
	if !strings.HasPrefix(s[j:], prefix) {
		return false
	}
	k := j + len(prefix)
	if prefix == "" && (k >= len(s) || !isFlagNameStart(s[k])) {
		return false
	}
	for k < len(s) && isFlagNameChar(s[k]) {
		k++
	}
	return k < len(s) && s[k] == '='
}
