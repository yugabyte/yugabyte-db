// Copyright (c) YugabyteDB, Inc.

package serverconfredact

// redactStandardJSONQuotedStringValue redacts "field": "value". Linear scan rather than a regex,
// so that a huge gflag value cannot blow up matching.
func redactStandardJSONQuotedStringValue(input, field, replacement string) string {
	keyQuoted := `"` + field + `"`
	from := 0
	out := make([]byte, 0, len(input))
	for from < len(input) {
		keyIdx := indexOf(input, keyQuoted, from)
		if keyIdx < 0 {
			out = append(out, input[from:]...)
			break
		}
		if !isStructuralJSONObjectKeyQuote(input, keyIdx) {
			out = append(out, input[from:keyIdx+1]...)
			from = keyIdx + 1
			continue
		}
		out = append(out, input[from:keyIdx]...)
		p := keyIdx + len(keyQuoted)
		for p < len(input) && isWhitespace(input[p]) {
			p++
		}
		if p >= len(input) || input[p] != ':' {
			out = append(out, input[keyIdx])
			from = keyIdx + 1
			continue
		}
		p++
		for p < len(input) && isWhitespace(input[p]) {
			p++
		}
		if p >= len(input) || input[p] != '"' {
			next := keyIdx + len(keyQuoted)
			out = append(out, input[keyIdx:next]...)
			from = next
			continue
		}
		valueContentStart := p + 1
		closingQuote := indexOfUnescapedDoubleQuote(input, valueContentStart)
		if closingQuote < 0 {
			out = append(out, input[keyIdx:]...)
			break
		}
		out = append(out, input[keyIdx:p+1]...)
		out = append(out, replacement...)
		out = append(out, '"')
		from = closingQuote + 1
	}
	return string(out)
}

// redactJSONBareValue redacts "field": bareValue, where the value is not a JSON string — a bare
// number, true/false, or null.
func redactJSONBareValue(input, field, replacement string) string {
	keyQuoted := `"` + field + `"`
	if !contains(input, keyQuoted) {
		return input
	}
	n := len(input)
	from := 0
	out := make([]byte, 0, n)
	for {
		keyIdx := indexOf(input, keyQuoted, from)
		if keyIdx < 0 {
			out = append(out, input[from:]...)
			break
		}
		if !isStructuralJSONObjectKeyQuote(input, keyIdx) {
			out = append(out, input[from:keyIdx+1]...)
			from = keyIdx + 1
			continue
		}
		p := keyIdx + len(keyQuoted)
		for p < n && isWhitespace(input[p]) {
			p++
		}
		if p >= n || input[p] != ':' {
			next := keyIdx + len(keyQuoted)
			out = append(out, input[from:next]...)
			from = next
			continue
		}
		p++
		for p < n && isWhitespace(input[p]) {
			p++
		}
		if p >= n || input[p] == '"' {
			next := keyIdx + len(keyQuoted)
			out = append(out, input[from:next]...)
			from = next
			continue
		}
		vc := input[p]
		if vc == ',' || vc == '}' || vc == ']' {
			next := keyIdx + len(keyQuoted)
			out = append(out, input[from:next]...)
			from = next
			continue
		}
		vend := p
		for vend < n && input[vend] != ',' && input[vend] != '}' && input[vend] != ']' && !isWhitespace(input[vend]) {
			vend++
		}
		out = append(out, input[from:p]...)
		out = append(out, replacement...)
		from = vend
	}
	return string(out)
}

// redactBackslashEscapedJSONQuotedKeyValue redacts \"field\": \"value\", i.e. a JSON object
// that has itself been embedded in a JSON string — the shape gflag values take once a host string
// or log line has been serialised a second time.
func redactBackslashEscapedJSONQuotedKeyValue(input, field, replacement string) string {
	escapedKey := `\"` + field + `\"`
	from := 0
	out := make([]byte, 0, len(input))
	for from < len(input) {
		keyIdx := indexOf(input, escapedKey, from)
		if keyIdx < 0 {
			out = append(out, input[from:]...)
			break
		}
		out = append(out, input[from:keyIdx]...)
		p := keyIdx + len(escapedKey)
		for p < len(input) && isWhitespace(input[p]) {
			p++
		}
		if p >= len(input) || input[p] != ':' {
			out = append(out, input[keyIdx])
			from = keyIdx + 1
			continue
		}
		p++
		for p < len(input) && isWhitespace(input[p]) {
			p++
		}
		if p+1 >= len(input) || input[p] != '\\' || input[p+1] != '"' {
			out = append(out, input[keyIdx])
			from = keyIdx + 1
			continue
		}
		valueContentStart := p + 2
		closingBackslash := indexOfClosingBackslashQuoteInEscapedJSONString(input, valueContentStart)
		if closingBackslash < 0 {
			out = append(out, input[keyIdx:]...)
			break
		}
		out = append(out, input[keyIdx:valueContentStart]...)
		out = append(out, replacement...)
		from = closingBackslash + 2
	}
	return string(out)
}

// isStructuralJSONObjectKeyQuote reports whether the quote at quoteIdx opens a real object key in
// raw JSON, as opposed to an escaped quote sitting inside a string value.
//
// This guard is load-bearing. A substring like \"ycql_ldap_bind_passwd\" inside a larger JSON
// string is not a key; treating it as one consumes the real closing quote and leaves output that
// no longer parses as JSON.
func isStructuralJSONObjectKeyQuote(s string, quoteIdx int) bool {
	if quoteIdx < 0 || quoteIdx >= len(s) || s[quoteIdx] != '"' {
		return false
	}
	if isBackslashEscapedDoubleQuote(s, quoteIdx) {
		return false
	}
	i := quoteIdx - 1
	for i >= 0 && isWhitespace(s[i]) {
		i--
	}
	if i < 0 {
		return true
	}
	c := s[i]
	return c == '{' || c == ',' || c == '['
}

// isBackslashEscapedDoubleQuote reports whether the quote at quoteIdx is escaped. An odd number of
// preceding backslashes means escaped; an even number means the backslashes escape each other and
// the quote is structural.
func isBackslashEscapedDoubleQuote(s string, quoteIdx int) bool {
	backslashes := 0
	for i := quoteIdx - 1; i >= 0 && s[i] == '\\'; i-- {
		backslashes++
	}
	return backslashes&1 == 1
}

// indexOfUnescapedDoubleQuote returns the index of the next structural closing quote at or after
// start, skipping \" escape pairs.
func indexOfUnescapedDoubleQuote(s string, start int) int {
	for i := start; i < len(s); {
		if s[i] == '"' {
			return i
		}
		if s[i] == '\\' && i+1 < len(s) {
			i += 2
			continue
		}
		i++
	}
	return -1
}

// indexOfClosingBackslashQuoteInEscapedJSONString returns the index of the \" that closes an
// escaped JSON string value beginning at start, or -1.
//
// A \" alone is ambiguous: it can close the value or be an escaped quote within it. It is treated
// as closing only when the next non-whitespace byte is a JSON terminator (} , ] ") or the input
// ends. Any other following byte means the value continues.
func indexOfClosingBackslashQuoteInEscapedJSONString(s string, start int) int {
	i := start
	n := len(s)
	for i < n {
		if i+1 < n && s[i] == '\\' && s[i+1] == '"' {
			after := i + 2
			for after < n && isWhitespace(s[after]) {
				after++
			}
			if after >= n {
				return i
			}
			c := s[after]
			if c == '}' || c == ',' || c == ']' || c == '"' {
				return i
			}
			i += 2
			continue
		}
		i++
	}
	return -1
}
