// Copyright (c) YugabyteDB, Inc.

package serverconfredact

// redactKeyEqualsValue redacts field=value wherever it appears — the gflags/conf/log-line form.
// Three quoting styles are handled: CSV-quoted ("value"), escaped-CSV-quoted (\"value\"), and
// unquoted.
func redactKeyEqualsValue(input, field, replacement string) string {
	needle := field + "="
	pos := indexOf(input, needle, 0)
	if pos < 0 {
		return input
	}
	out := make([]byte, 0, len(input))
	from := 0
	for pos >= 0 {
		out = append(out, input[from:pos+len(needle)]...)
		vs := pos + len(needle)
		if vs >= len(input) {
			from = vs
			break
		}
		c := input[vs]
		var ve int
		switch {
		case c == '"':
			ve = scanPastCSVQuotedValue(input, vs+1)
			out = append(out, '"')
			out = append(out, replacement...)
			out = append(out, '"')
		case c == '\\' && vs+1 < len(input) && input[vs+1] == '"':
			ve = scanPastEscapedCSVQuotedValue(input, vs+2)
			out = append(out, '\\', '"')
			out = append(out, replacement...)
			out = append(out, '\\', '"')
		default:
			ve = findUnquotedValueEnd(input, vs)
			out = append(out, replacement...)
		}
		from = ve
		pos = indexOf(input, needle, from)
	}
	out = append(out, input[from:]...)
	return string(out)
}

// scanPastCSVQuotedValue returns the index just past the closing " of a CSV-quoted value,
// treating a doubled "" as an embedded literal quote rather than a terminator.
func scanPastCSVQuotedValue(s string, start int) int {
	for i := start; i < len(s); {
		if s[i] == '"' {
			if i+1 < len(s) && s[i+1] == '"' {
				i += 2
			} else {
				return i + 1
			}
		} else {
			i++
		}
	}
	return len(s)
}

// scanPastEscapedCSVQuotedValue is scanPastCSVQuotedValue one escape level deeper: the delimiter
// is \" and a doubled \"\" is an embedded literal quote.
func scanPastEscapedCSVQuotedValue(s string, start int) int {
	for i := start; i < len(s); {
		if i+1 < len(s) && s[i] == '\\' && s[i+1] == '"' {
			if i+3 < len(s) && s[i+2] == '\\' && s[i+3] == '"' {
				i += 4
			} else {
				return i + 2
			}
		} else {
			i++
		}
	}
	return len(s)
}

// findUnquotedValueEnd returns the end of an unquoted value following key=.
//
// The terminator is context-dependent, and each rule below exists because of a real input shape:
//
//   - A newline always ends the value.
//   - A lone " ends it only when followed (after optional whitespace) by a JSON delimiter
//     } , ] : or end of input. Bare quotes are legal *inside* conf and log values — a password
//     may literally be h*Y"abc"P — so a quote is only a boundary in JSON-embedded context.
//   - A doubled "" is an embedded literal quote and is skipped.
//   - A backslash is a literal character, never a stop, so a password like pass\word is consumed
//     whole. The " rule above already covers the JSON-embedded case without needing it.
//   - A comma ends the value when followed by an identifier of at least 3 characters and then '='
//     (the next gflag in conf or combined-gflags format).
//
// The 3-character minimum is the subtle part: it stops LDAP distinguished-name components — OU=,
// DC=, CN= — from being mistaken for the next key. Without it, redacting a field such as
// ycql_ldap_bind_dn would cut the value short at the first DN component and leave the remainder
// of the secret in the file.
func findUnquotedValueEnd(s string, start int) int {
	n := len(s)
	for i := start; i < n; i++ {
		c := s[i]
		switch c {
		case '\n', '\r':
			return i
		case '"':
			if i+1 < n && s[i+1] == '"' {
				i++
				continue
			}
			next := byte(0)
			if i+1 < n {
				next = s[i+1]
			}
			if next == '}' || next == ',' || next == ']' || next == ':' || next == 0 {
				return i
			}
			if isWhitespace(next) {
				j := i + 2
				for j < n && isWhitespace(s[j]) {
					j++
				}
				lookahead := byte(0)
				if j < n {
					lookahead = s[j]
				}
				if lookahead == '}' || lookahead == ',' || lookahead == ']' || lookahead == ':' || lookahead == 0 {
					return i
				}
			}
		case ',':
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
					return i
				}
			}
			if j+1 < n && s[j] == '-' && s[j+1] == '-' {
				return i
			}
		}
	}
	return n
}
