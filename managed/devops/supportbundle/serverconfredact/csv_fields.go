// Copyright (c) YugabyteDB, Inc.

package serverconfredact

import (
	"unicode"
	"unicode/utf8"
)

// csvField is a CSV field: its decoded text, and where each byte of it starts in the raw value.
type csvField struct {
	text     string
	quoted   bool
	rawIndex []int
}

// splitCSVFields splits a value without line breaks into fields as Commons CSV's DEFAULT format
// does, or returns nil where Commons CSV rejects it. It ports CsvFieldSplitter.split, which YBA
// uses for the same value: GFlagsUtil.mergeCSVs re-encodes ysql_hba_conf_csv with those rules.
func splitCSVFields(value string) []csvField {
	var fields []csvField
	n := len(value)
	raw := make([]int, n)
	var text []byte
	p := 0
	for {
		text = text[:0]
		quoted := p < n && value[p] == '"'
		i := p
		if quoted {
			i++
		}
		for {
			if i >= n {
				if quoted {
					return nil
				}
				break
			}
			c := value[i]
			if !quoted && c == ',' {
				break
			}
			if quoted && c == '"' {
				if i+1 < n && value[i+1] == '"' {
					raw[len(text)] = i
					text = append(text, '"')
					i += 2
					continue
				}
				i++
				for i < n && value[i] != ',' {
					r, size := utf8.DecodeRuneInString(value[i:])
					if !isJavaWhitespace(r) {
						break
					}
					i += size
				}
				if i < n && value[i] != ',' {
					return nil
				}
				break
			}
			raw[len(text)] = i
			text = append(text, c)
			i++
		}
		fields = append(fields, csvField{string(text), quoted, append([]int(nil), raw[:len(text)]...)})
		if i >= n {
			return fields
		}
		p = i + 1
	}
}

// rawEnd is where byte i of the field ends in the raw value; a quote in a quoted field is doubled.
func (f csvField) rawEnd(i int) int {
	if f.quoted && f.text[i] == '"' {
		return f.rawIndex[i] + 2
	}
	return f.rawIndex[i] + 1
}

// isJavaWhitespace is Java's Character.isWhitespace, which CsvFieldSplitter uses after a closing
// quote.
func isJavaWhitespace(r rune) bool {
	switch r {
	case '\t', '\n', '\v', '\f', '\r', '\x1c', '\x1d', '\x1e', '\x1f':
		return true
	case '\u00a0', '\u2007', '\u202f':
		return false
	}
	return unicode.In(r, unicode.Zs, unicode.Zl, unicode.Zp)
}
