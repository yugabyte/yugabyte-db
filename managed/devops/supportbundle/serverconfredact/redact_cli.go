// Copyright (c) YugabyteDB, Inc.

package serverconfredact

// redactCliFlagValue redacts --field value in every CLI quoting style. These shapes come from
// command arrays that YBA logged while invoking a process, so the same flag may appear plain,
// single-quoted, or escaped-double-quoted in one file.
func redactCliFlagValue(input, field, replacement string) string {
	if !contains(input, "--"+field) {
		return input
	}
	input = redactCliFlagPlain(input, field, replacement)
	input = redactCliFlagSingleQuoted(input, field, replacement)
	input = redactCliFlagEscDblQuoted(input, field, replacement)
	return input
}

// redactCliFlagPlain redacts unquoted "--field, value" and "--field value", stopping at the next
// " --" or ", --" separator. Occurrences already inside a quoted context are skipped and left to
// the quoted-form redactors below, so a flag is never redacted twice with mismatched quoting.
func redactCliFlagPlain(input, field, replacement string) string {
	token := "--" + field
	n := len(input)
	from := 0
	out := make([]byte, 0, n)
	for {
		idx := indexOf(input, token, from)
		if idx < 0 {
			out = append(out, input[from:]...)
			break
		}
		before := byte(0)
		if idx > 0 {
			before = input[idx-1]
		}
		if before == '\'' || (before == '"' && idx >= 2 && input[idx-2] == '\\') {
			out = append(out, input[from:idx+1]...)
			from = idx + 1
			continue
		}
		p := idx + len(token)
		hasSep := false
		if p < n && input[p] == ',' {
			p++
			hasSep = true
		}
		for p < n && input[p] == ' ' {
			p++
			hasSep = true
		}
		if !hasSep || p >= n {
			out = append(out, input[from:idx+len(token)]...)
			from = idx + len(token)
			continue
		}
		valueEnd := findCliFlagValueEnd(input, p)
		out = append(out, input[from:idx+len(token)]...)
		out = append(out, ", "...)
		out = append(out, replacement...)
		from = valueEnd
	}
	return string(out)
}

// findCliFlagValueEnd returns the start of the next CLI flag separator — " --", "\n--", or
// ", --" — or the end of the string.
func findCliFlagValueEnd(s string, start int) int {
	n := len(s)
	for i := start; i < n; i++ {
		c := s[i]
		if (c == ' ' || c == '\n') && i+2 < n && s[i+1] == '-' && s[i+2] == '-' {
			return i
		}
		if c == ',' {
			j := i + 1
			for j < n && s[j] == ' ' {
				j++
			}
			if j+1 < n && s[j] == '-' && s[j+1] == '-' {
				return i
			}
		}
	}
	return n
}

// redactCliFlagSingleQuoted redacts '--field', 'value' as emitted by Python/Ansible command logs.
func redactCliFlagSingleQuoted(input, field, replacement string) string {
	prefix := `'--` + field + `',`
	if !contains(input, prefix) {
		return input
	}
	n := len(input)
	from := 0
	out := make([]byte, 0, n)
	for {
		idx := indexOf(input, prefix, from)
		if idx < 0 {
			out = append(out, input[from:]...)
			break
		}
		out = append(out, input[from:idx+len(prefix)]...)
		p := idx + len(prefix)
		for p < n && input[p] == ' ' {
			p++
		}
		if p < n && input[p] == '\'' {
			valueEnd := indexOf(input, `'`, p+1)
			if valueEnd < 0 {
				valueEnd = n - 1
			}
			out = append(out, ' ')
			out = append(out, replacement...)
			from = valueEnd + 1
		} else {
			valueEnd := p
			for valueEnd < n && input[valueEnd] != ',' && input[valueEnd] != '\'' {
				valueEnd++
			}
			out = append(out, ' ')
			out = append(out, replacement...)
			from = valueEnd
		}
	}
	return string(out)
}

// redactCliFlagEscDblQuoted redacts \"--field\", \"value\" — the same array after being
// serialised into a JSON string.
func redactCliFlagEscDblQuoted(input, field, replacement string) string {
	prefix := `\"--` + field + `\",`
	if !contains(input, prefix) {
		return input
	}
	n := len(input)
	from := 0
	out := make([]byte, 0, n)
	for {
		idx := indexOf(input, prefix, from)
		if idx < 0 {
			out = append(out, input[from:]...)
			break
		}
		out = append(out, input[from:idx+len(prefix)]...)
		p := idx + len(prefix)
		for p < n && input[p] == ' ' {
			p++
		}
		if p+1 < n && input[p] == '\\' && input[p+1] == '"' {
			vs := p + 2
			ve := vs
			for ve+1 < n && !(input[ve] == '\\' && input[ve+1] == '"') {
				ve++
			}
			out = append(out, ' ')
			out = append(out, replacement...)
			if ve+1 < n {
				from = ve + 2
			} else {
				// No closing \": the value runs to end of input. Advancing past n would make the
				// next iteration slice input[from:] out of bounds.
				from = n
			}
		} else {
			valueEnd := p
			for valueEnd < n && input[valueEnd] != ',' && input[valueEnd] != ' ' {
				valueEnd++
			}
			out = append(out, ' ')
			out = append(out, replacement...)
			from = valueEnd
		}
	}
	return string(out)
}

// redactPythonDictValue redacts the value in a Python repr dict entry,
// {'key': 'field', 'value': 'secret'}, which is how logged Ansible output renders gflag pairs.
func redactPythonDictValue(input, field, replacement string) string {
	keyMarker := `'key': '` + field + `'`
	if !contains(input, keyMarker) {
		return input
	}
	n := len(input)
	from := 0
	out := make([]byte, 0, n)
	for {
		idx := indexOf(input, keyMarker, from)
		if idx < 0 {
			out = append(out, input[from:]...)
			break
		}
		p := idx + len(keyMarker)
		for p < n && (input[p] == ' ' || input[p] == '\t') {
			p++
		}
		if p >= n || input[p] != ',' {
			out = append(out, input[from:idx+1]...)
			from = idx + 1
			continue
		}
		p++
		for p < n && (input[p] == ' ' || input[p] == '\t') {
			p++
		}
		valueMarker := `'value': '`
		if p+len(valueMarker) > n || input[p:p+len(valueMarker)] != valueMarker {
			out = append(out, input[from:idx+1]...)
			from = idx + 1
			continue
		}
		p += len(valueMarker)
		valueEnd := indexOf(input, `'`, p)
		if valueEnd < 0 {
			// No closing quote: treat the last byte as the terminator so from lands on n rather
			// than past it, as the single-quoted CLI branch above already does.
			valueEnd = n - 1
		}
		out = append(out, input[from:p]...)
		out = append(out, replacement...)
		out = append(out, '\'')
		from = valueEnd + 1
	}
	return string(out)
}
