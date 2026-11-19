// Copyright (c) YugabyteDB, Inc.

package serverconfredact

import (
	"fmt"
	"math"
	"sort"
	"strings"
	"unicode"
	"unicode/utf8"
)

// This file ports YBA's HbaConfRedactor.java, which redacts ldapbindpasswd in ysql_hba_conf_csv by
// reading the value the way the DB does: CSV fields become ysql_hba.conf lines, which are joined
// at a trailing backslash and tokenized as hba.c does. hba_conf_test.go checks that both produce
// the same output, so keep the two in step.

const (
	hbaKey             = "ldapbindpasswd"
	hbaOptionPrefix    = hbaKey + "="
	hbaGflagLinePrefix = "--ysql_hba_conf_csv="
)

// hbaConnectionTypes are the connection types parse_hba_line in hba.c accepts.
var hbaConnectionTypes = map[string]bool{
	"local": true, "host": true, "hostssl": true, "hostnossl": true, "hostgssenc": true,
	"hostnogssenc": true,
}

// hbaOptionNames are the option names parse_hba_auth_opt in hba.c accepts.
var hbaOptionNames = map[string]bool{
	"map": true, "clientcert": true, "clientname": true, "pamservice": true,
	"pam_use_hostname": true, "ldapurl": true, "ldaptls": true, "ldapscheme": true,
	"ldapserver": true, "ldapport": true, "ldapbinddn": true, "ldapbindpasswd": true,
	"ldapsearchattribute": true, "ldapsearchfilter": true, "ldapbasedn": true, "ldapprefix": true,
	"ldapsuffix": true, "krb_realm": true, "include_realm": true, "compat_realm": true,
	"upn_username": true, "radiusservers": true, "radiusports": true, "radiussecrets": true,
	"radiusidentifiers": true, "jwt_jwks_path": true, "jwt_jwks_url": true, "jwt_audiences": true,
	"jwt_issuers": true, "jwt_matching_claim_key": true,
}

// hbaLine is an hba.conf line, with the field and field offset each byte came from.
type hbaLine struct {
	text   string
	field  []int
	offset []int
}

// hbaToken is a token as next_token returns it, with the line position of each byte of its text.
type hbaToken struct {
	start, end int
	text       string
	pos        []int
	inQuotes   []bool
}

// hbaTokens are the tokens of a line, and where its comment starts (-1 if none).
type hbaTokens struct {
	tokens       []hbaToken
	commentStart int
}

type hbaEdit struct {
	start, end  int
	replacement string
}

// redactHbaConfCSV redacts every ldapbindpasswd value in a ysql_hba_conf_csv value.
func redactHbaConfCSV(value string) string {
	// Matched loosely: lines are not joined yet, and a continuation may split the name.
	if indexOfHbaKey(value, 0, true) < 0 {
		return value
	}
	fields := hbaConfFields(value)
	if fields == nil {
		// The DB rejects the whole value, and there are no fields to stop at.
		return redactHbaToEnd(value, true)
	}
	fieldEdits := make([][]hbaEdit, len(fields))
	// After a line redacted to its end, lines that don't start a rule may be the rest of the
	// password, split off by an unquoted comma. Postgres rejects or ignores them.
	afterRedactedEnd := false
	for _, line := range joinHbaLines(fields) {
		text := line.text
		var edits []hbaEdit
		switch {
		case indexOfHbaKey(text, 0, false) >= 0:
			edits = hbaLineEdits(text)
			afterRedactedEnd = len(edits) > 0 && edits[len(edits)-1].end == len(text)
		case afterRedactedEnd && !startsHbaRule(text):
			if !isAllHbaBlank(text) {
				edits = []hbaEdit{{0, len(text), SecretReplacement}}
			}
		default:
			afterRedactedEnd = false
			continue
		}
		for _, edit := range edits {
			addFieldEdits(line, edit, fieldEdits)
		}
	}
	return spliceFieldEdits(value, fields, fieldEdits)
}

// redactLdapBindPasswdText redacts ldapbindpasswd in text with no structure to rely on: from the
// name to the end of the line, and the lines after it that are indented with more spaces, because
// Helm folds long YAML values onto those. A line that doesn't start a YAML or JSON entry, below an
// entry whose value isn't a JSON array or object or a YAML literal block, continues that entry's
// value, so the lines after it indented past the entry are redacted too. Where a ':' ends the line
// after the name, as after a key whose value starts below, the next line that isn't blank is
// redacted whatever its indent, even below a log line that only mentions the name. A
// --ysql_hba_conf_csv= gflag line is redacted like the gflag value, and folds the same way when that
// redaction reaches the end of the line.
func redactLdapBindPasswdText(text string) string {
	if indexOfHbaKey(text, 0, true) < 0 {
		return text
	}
	var out strings.Builder
	out.Grow(len(text))
	// Earlier lines, each indented less than the one appended after it.
	type parent struct{ indent, anchor int }
	var parents []parent
	foldIndent := -1
	valueBelow := false
	for p := 0; p < len(text); {
		next, end := hbaTextLine(text, p)
		indent := 0
		for p+indent < end && text[p+indent] == ' ' {
			indent++
		}
		blank := p+indent == end
		line := text[p:end]
		entryValue, parentAnchor := -1, -1
		if !blank {
			for len(parents) > 0 && parents[len(parents)-1].indent >= indent {
				parents = parents[:len(parents)-1]
			}
			if len(parents) > 0 {
				parentAnchor = parents[len(parents)-1].anchor
			}
			// anchor is the indent of the entry whose value this line continues, or -1.
			anchor := parentAnchor
			if entryValue = entryValueStart(line); entryValue >= 0 {
				anchor = indent
				// The lines of a JSON array or object, or of a YAML literal block, aren't a folded value.
				if entryValue < len(line) && strings.IndexByte("[{|", line[entryValue]) >= 0 {
					anchor = -1
				}
			}
			parents = append(parents, parent{indent, anchor})
		}
		if !blank && (valueBelow || foldIndent >= 0 && indent > foldIndent) {
			out.WriteString(text[p : p+indent])
			out.WriteString(SecretReplacement)
			valueBelow = false
		} else {
			foldIndent = -1
			if valueStart := hbaGflagValueStart(line); valueStart >= 0 {
				value := line[valueStart:]
				redacted := redactHbaConfCSV(value)
				if redacted != value && endsRedacted(redacted) {
					foldIndent = indent
				}
				out.WriteString(line[:valueStart])
				out.WriteString(redacted)
			} else {
				redacted := redactHbaToEnd(line, true)
				if redacted != line {
					foldIndent = indent
					if entryValue < 0 && parentAnchor >= 0 {
						foldIndent = parentAnchor
					}
					valueBelow = hbaValueStartsBelow(line)
				}
				out.WriteString(redacted)
			}
		}
		out.WriteString(text[end:next])
		p = next
	}
	return out.String()
}

// endsRedacted reports whether s ends with REDACTED, past any closing quotes.
func endsRedacted(s string) bool {
	return strings.HasSuffix(strings.TrimRight(s, `"`), SecretReplacement)
}

// entryValueStart is, if line starts past its blanks with a YAML or JSON key and its ':', or a YAML
// sequence item, where its value starts past the blanks after the ':' or '-'; else -1.
func entryValueStart(line string) int {
	k := 0
	for k < len(line) && isHbaBlank(line[k]) {
		k++
	}
	n := len(line)
	var j int
	if k < n && line[k] == '-' && (k+1 == n || isHbaBlank(line[k+1])) {
		j = k + 1
	} else {
		quoted := k < n && line[k] == '"'
		j = k
		if quoted {
			closing := strings.IndexByte(line[k+1:], '"')
			if closing < 0 {
				return -1
			}
			j = k + 1 + closing + 1
		} else {
			for j < n && isKeyChar(line[j]) {
				j++
			}
			if j == k {
				return -1
			}
		}
		for j < n && isHbaBlank(line[j]) {
			j++
		}
		// A plain YAML key needs a blank after its ':', so that "host:port" isn't one.
		if j == n || line[j] != ':' || !(quoted || j+1 == n || isHbaBlank(line[j+1])) {
			return -1
		}
		j++
	}
	for j < n && isHbaBlank(line[j]) {
		j++
	}
	return j
}

// hbaValueStartsBelow reports whether a ':' ends line after an ldapbindpasswd in it, past any
// quotes.
func hbaValueStartsBelow(line string) bool {
	n := len(line)
	for at := indexOfHbaKey(line, 0, true); at >= 0; at = indexOfHbaKey(line, at+1, true) {
		j := matchHbaKey(line, at, true)
		for j < n && (line[j] == '"' || line[j] == '\'' || isHbaBlank(line[j])) {
			j++
		}
		if j < n && line[j] == ':' {
			j++
			for j < n && isHbaBlank(line[j]) {
				j++
			}
			if j == n {
				return true
			}
		}
	}
	return false
}

func isKeyChar(c byte) bool {
	return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_' ||
		c == '-' || c == '.'
}

// hbaTextLine returns where the line starting at p ends, before and after its line break.
func hbaTextLine(text string, p int) (next, end int) {
	next = len(text)
	if i := strings.IndexByte(text[p:], '\n'); i >= 0 {
		next = p + i + 1
	}
	end = next
	for end > p && (text[end-1] == '\n' || text[end-1] == '\r') {
		end--
	}
	return next, end
}

// hbaGflagValueStart is where the value starts if line is a --ysql_hba_conf_csv= gflag, else -1.
func hbaGflagValueStart(line string) int {
	k := 0
	for k < len(line) && isHbaBlank(line[k]) {
		k++
	}
	if !strings.HasPrefix(line[k:], hbaGflagLinePrefix) {
		return -1
	}
	return k + len(hbaGflagLinePrefix)
}

func hbaConfFields(value string) []csvField {
	if strings.ContainsAny(value, "\r\n") {
		return nil
	}
	return splitCSVFields(value)
}

// joinHbaLines joins fields into lines the way tokenize_auth_file joins lines ending in a
// backslash.
func joinHbaLines(fields []csvField) []hbaLine {
	var lines []hbaLine
	var text []byte
	var field, offset []int
	lastBackslash := 0
	for k, f := range fields {
		for j := 0; j < len(f.text); j++ {
			field = append(field, k)
			offset = append(offset, j)
			text = append(text, f.text[j])
		}
		// The last field would join the lines the DB generates, which leaves it invalid; its
		// backslash stays in the line so that a value ending in one is redacted whole.
		n := len(text)
		if k+1 < len(fields) && n > lastBackslash && text[n-1] == '\\' {
			text, field, offset = text[:n-1], field[:n-1], offset[:n-1]
			lastBackslash = n - 1
			continue
		}
		lines = append(lines, hbaLine{string(text), field, offset})
		text, field, offset = nil, nil, nil
		lastBackslash = 0
	}
	return lines
}

// tokenizeHba tokenizes one line as next_token in hba.c does.
func tokenizeHba(line string) hbaTokens {
	var tokens []hbaToken
	n := len(line)
	pos := make([]int, n)
	inQuotes := make([]bool, n)
	var text []byte
	i := 0
	for i < n {
		for i < n && (isHbaBlank(line[i]) || line[i] == ',') {
			i++
		}
		start := i
		text = text[:0]
		inQuote, wasQuote, sawQuote := false, false, false
		for i < n && (!isHbaBlank(line[i]) || inQuote) {
			c := line[i]
			if !inQuote && c == '#' {
				if sawQuote || len(text) > 0 {
					tokens = append(tokens, newHbaToken(start, i, text, pos, inQuotes))
				}
				return hbaTokens{tokens, i}
			}
			if !inQuote && c == ',' {
				break
			}
			if c != '"' || wasQuote {
				pos[len(text)] = i
				inQuotes[len(text)] = inQuote
				text = append(text, c)
			}
			wasQuote = inQuote && c == '"' && !wasQuote
			if c == '"' {
				inQuote = !inQuote
				sawQuote = true
			}
			i++
		}
		if sawQuote || len(text) > 0 {
			tokens = append(tokens, newHbaToken(start, i, text, pos, inQuotes))
		}
	}
	return hbaTokens{tokens, -1}
}

func newHbaToken(start, end int, text []byte, pos []int, inQuotes []bool) hbaToken {
	return hbaToken{
		start:    start,
		end:      end,
		text:     string(text),
		pos:      append([]int(nil), pos[:len(text)]...),
		inQuotes: append([]bool(nil), inQuotes[:len(text)]...),
	}
}

// hbaLineEdits returns the edits for one line, in line positions.
func hbaLineEdits(line string) []hbaEdit {
	tokens := tokenizeHba(line)
	var options []hbaToken
	var exact []hbaEdit
	toEnd := math.MaxInt
	for k, token := range tokens.tokens {
		if !strings.HasPrefix(token.text, hbaOptionPrefix) {
			continue
		}
		options = append(options, token)
		valueStart := token.pos[len(hbaKey)] + 1
		if onlyHbaOptionsFollow(tokens.tokens, k) {
			exact = append(exact, hbaEdit{valueStart, token.end, hbaReplacement(line, token, valueStart)})
		} else {
			toEnd = min(toEnd, valueStart)
		}
	}
	// The comment may be the rest of a password with an unquoted '#'.
	if len(options) > 0 && tokens.commentStart >= 0 {
		toEnd = min(toEnd, tokens.commentStart+1)
	}
	for from := indexOfHbaKey(line, 0, false); from >= 0; {
		nameEnd := matchHbaKey(line, from, false)
		if from < toEnd && !insideHbaToken(options, from) {
			toEnd = min(toEnd, hbaKeptEnd(line, nameEnd))
		}
		from = indexOfHbaKey(line, nameEnd, false)
	}
	var edits []hbaEdit
	for _, edit := range exact {
		if edit.start >= toEnd {
			break
		}
		if edit.end > toEnd {
			toEnd = edit.start
			break
		}
		edits = append(edits, edit)
	}
	if toEnd != math.MaxInt {
		edits = append(edits, hbaEdit{toEnd, len(line), SecretReplacement})
	}
	return edits
}

func insideHbaToken(tokens []hbaToken, at int) bool {
	for _, t := range tokens {
		if t.start <= at && at < t.end {
			return true
		}
	}
	return false
}

func startsHbaRule(line string) bool {
	tokens := tokenizeHba(line).tokens
	return len(tokens) > 0 && hbaConnectionTypes[tokens[0].text]
}

// onlyHbaOptionsFollow reports whether every token after tokens[k] is a name=value option Postgres
// knows.
func onlyHbaOptionsFollow(tokens []hbaToken, k int) bool {
	for _, t := range tokens[k+1:] {
		eq := strings.IndexByte(t.text, '=')
		if eq < 0 || !hbaOptionNames[t.text[:eq]] {
			return false
		}
	}
	return true
}

// hbaReplacement is the replacement for a value, quoted so that the token stays well formed. A
// value wrapped in empty quoted strings, ""secret"", keeps that shape: it is how values written as
// if CSV-escaped read.
func hbaReplacement(line string, token hbaToken, valueStart int) string {
	if token.inQuotes[len(hbaKey)] {
		return SecretReplacement + `"`
	}
	raw := line[valueStart:token.end]
	if len(raw) >= 4 && strings.HasPrefix(raw, `""`) && strings.HasSuffix(raw, `""`) {
		return `""` + SecretReplacement + `""`
	}
	if strings.HasPrefix(raw, `"`) {
		return `"` + SecretReplacement + `"`
	}
	return SecretReplacement
}

// redactHbaToEnd redacts from the first ldapbindpasswd in s to the end of s.
func redactHbaToEnd(s string, freeText bool) string {
	at := indexOfHbaKey(s, 0, freeText)
	if at < 0 {
		return s
	}
	return s[:hbaKeptEnd(s, matchHbaKey(s, at, freeText))] + SecretReplacement
}

// hbaKeptEnd is the end of what is kept after ldapbindpasswd text ending at nameEnd: blanks and an
// '=', if any.
func hbaKeptEnd(s string, nameEnd int) int {
	j := nameEnd
	for j < len(s) && isHbaBlank(s[j]) {
		j++
	}
	if j < len(s) && s[j] == '=' {
		return j + 1
	}
	return nameEnd
}

// indexOfHbaKey is the index of the next ldapbindpasswd at or after from, ignoring case and any
// quotes inside it (and, for free text, the backslashes and commas that escaping or a continuation
// adds), or -1.
func indexOfHbaKey(s string, from int, freeText bool) int {
	for i := from; i < len(s); i++ {
		if (s[i] == 'l' || s[i] == 'L') && matchHbaKey(s, i, freeText) >= 0 {
			return i
		}
	}
	return -1
}

// matchHbaKey is the end of ldapbindpasswd starting at i, or -1. Case is folded per rune, as Java
// folds per char, so that U+0130 matches 'i' in both.
func matchHbaKey(s string, i int, freeText bool) int {
	j := i
	for k := 0; k < len(hbaKey); k++ {
		for k > 0 && j < len(s) && isHbaKeySkipped(s[j], freeText) {
			j++
		}
		if j >= len(s) {
			return -1
		}
		r, size := utf8.DecodeRuneInString(s[j:])
		if unicode.ToLower(r) != rune(hbaKey[k]) {
			return -1
		}
		j += size
	}
	return j
}

func isHbaKeySkipped(c byte, freeText bool) bool {
	return c == '"' || (freeText && (c == '\\' || c == ','))
}

// isHbaBlank is pg_isblank.
func isHbaBlank(c byte) bool {
	return c == ' ' || c == '\t' || c == '\r'
}

func isAllHbaBlank(s string) bool {
	for i := 0; i < len(s); i++ {
		if !isHbaBlank(s[i]) {
			return false
		}
	}
	return true
}

// addFieldEdits maps a line edit onto the fields it covers; only the first gets the replacement.
func addFieldEdits(line hbaLine, edit hbaEdit, fieldEdits [][]hbaEdit) {
	if edit.start == edit.end {
		// A value or line end is always preceded by at least the option name.
		before := edit.start - 1
		offset := line.offset[before] + 1
		f := line.field[before]
		fieldEdits[f] = append(fieldEdits[f], hbaEdit{offset, offset, edit.replacement})
		return
	}
	replacement := edit.replacement
	for i := edit.start; i < edit.end; {
		f := line.field[i]
		j := i
		for j+1 < edit.end && line.field[j+1] == f {
			j++
		}
		fieldEdits[f] = append(fieldEdits[f], hbaEdit{line.offset[i], line.offset[j] + 1, replacement})
		replacement = ""
		i = j + 1
	}
}

// spliceFieldEdits applies field edits to the raw value, re-encoding replacements for quoted
// fields.
func spliceFieldEdits(value string, fields []csvField, fieldEdits [][]hbaEdit) string {
	var rawEdits []hbaEdit
	for k, f := range fields {
		for _, edit := range fieldEdits[k] {
			var rawStart int
			if edit.start < len(f.text) {
				rawStart = f.rawIndex[edit.start]
			} else {
				rawStart = f.rawEnd(edit.start - 1)
			}
			rawEnd := rawStart
			if edit.end > edit.start {
				rawEnd = f.rawEnd(edit.end - 1)
			}
			replacement := edit.replacement
			if f.quoted {
				replacement = strings.ReplaceAll(replacement, `"`, `""`)
			}
			rawEdits = append(rawEdits, hbaEdit{rawStart, rawEnd, replacement})
		}
	}
	if len(rawEdits) == 0 {
		return value
	}
	sort.SliceStable(rawEdits, func(a, b int) bool { return rawEdits[a].start < rawEdits[b].start })
	var out strings.Builder
	out.Grow(len(value))
	last := 0
	for _, edit := range rawEdits {
		out.WriteString(value[last:edit.start])
		out.WriteString(edit.replacement)
		last = edit.end
	}
	out.WriteString(value[last:])
	return out.String()
}

// verifyLdapBindPasswd checks redactLdapBindPasswdText's output by what Postgres would read, not by
// where the redactor put its edits. In a ysql_hba_conf_csv rule, every ldapbindpasswd option must
// read REDACTED and be followed only by options Postgres knows, a comment beside one must read
// REDACTED, and the lines after a rule that ends redacted must be blank or REDACTED unless they
// start a rule. Any other ldapbindpasswd text must be followed by REDACTED and nothing else on its
// line. Reading the value uses the redactor's parsers, which would share a mistake with it, so
// ldapBindPasswdValuesRedacted checks each line without them too.
func verifyLdapBindPasswd(text string) error {
	if indexOfHbaKey(text, 0, true) < 0 {
		return nil
	}
	for p, lineNo := 0, 1; p < len(text); lineNo++ {
		next, end := hbaTextLine(text, p)
		line := text[p:end]
		var ok bool
		// The backstop reads CSV quotes from the start of the gflag value, where its first field starts.
		hba := line
		if valueStart := hbaGflagValueStart(line); valueStart >= 0 {
			hba = line[valueStart:]
			ok = hbaConfRedacted(hba)
		} else {
			ok = redactedToEnd(line, true)
		}
		if !ok || !ldapBindPasswdValuesRedacted(hba) {
			return fmt.Errorf("%s %w on line %d", hbaKey, ErrUnredacted, lineNo)
		}
		p = next
	}
	return nil
}

// ldapBindPasswdValuesRedacted reports whether every ldapbindpasswd in s that is followed by '=' is
// followed by REDACTED, past any quotes, backslashes and commas. It parses nothing but the name,
// which it matches in any case, with skipHbaKeyGap's gaps between its letters and before the '='.
func ldapBindPasswdValuesRedacted(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] != 'l' && s[i] != 'L' {
			continue
		}
		nameEnd := matchSplitHbaKey(s, i)
		if nameEnd < 0 {
			continue
		}
		j := skipHbaKeyGap(s, nameEnd)
		for j < len(s) && isHbaBlank(s[j]) {
			j++
		}
		if j >= len(s) || s[j] != '=' {
			continue
		}
		v := j + 1
		for v < len(s) && strings.IndexByte(`"'\,`, s[v]) >= 0 {
			v++
		}
		if !strings.HasPrefix(s[v:], SecretReplacement) {
			return false
		}
	}
	return true
}

// matchSplitHbaKey is the end of ldapbindpasswd starting at i, with skipHbaKeyGap's gaps between
// its letters, or -1.
func matchSplitHbaKey(s string, i int) int {
	j := i
	for k := 0; k < len(hbaKey); k++ {
		if k > 0 {
			j = skipHbaKeyGap(s, j)
		}
		if j >= len(s) {
			return -1
		}
		r, size := utf8.DecodeRuneInString(s[j:])
		if unicode.ToLower(r) != rune(hbaKey[k]) {
			return -1
		}
		j += size
	}
	return j
}

// skipHbaKeyGap skips quotes and continuations from j. A continuation is a backslash then a comma
// outside CSV quotes, with quotes around either: two fields that joinHbaLines joins.
func skipHbaKeyGap(s string, j int) int {
	for {
		for j < len(s) && s[j] == '"' {
			j++
		}
		if j >= len(s) || s[j] != '\\' {
			return j
		}
		m := j + 1
		for m < len(s) && s[m] == '"' {
			m++
		}
		if m >= len(s) || s[m] != ',' || inCSVQuotes(s, m) {
			return j
		}
		j = m + 1
	}
}

// inCSVQuotes reports whether byte m of s is inside a quoted field, reading s from its start as
// splitCSVFields does: a quote opens a field only as its first byte, and "" inside one is a quote.
func inCSVQuotes(s string, m int) bool {
	quoted, fieldStart := false, true
	for i := 0; i < m; i++ {
		switch c := s[i]; {
		case quoted:
			if c == '"' {
				if i+1 < m && s[i+1] == '"' {
					i++
				} else {
					quoted = false
				}
			}
		case c == ',':
			fieldStart = true
			continue
		case c == '"' && fieldStart:
			quoted = true
		}
		fieldStart = false
	}
	return quoted
}

// redactedToEnd reports whether s holds no ldapbindpasswd text, or only REDACTED after the first.
func redactedToEnd(s string, freeText bool) bool {
	at := indexOfHbaKey(s, 0, freeText)
	return at < 0 || s[hbaKeptEnd(s, matchHbaKey(s, at, freeText)):] == SecretReplacement
}

func hbaConfRedacted(value string) bool {
	if indexOfHbaKey(value, 0, true) < 0 {
		return true
	}
	fields := hbaConfFields(value)
	if fields == nil {
		return redactedToEnd(value, true)
	}
	afterRedactedEnd := false
	for _, line := range joinHbaLines(fields) {
		text := line.text
		switch {
		case indexOfHbaKey(text, 0, false) >= 0:
			ok, endsRedacted := hbaRuleRedacted(text)
			if !ok {
				return false
			}
			afterRedactedEnd = endsRedacted
		case afterRedactedEnd && !startsHbaRule(text):
			if !isAllHbaBlank(text) && text != SecretReplacement {
				return false
			}
		default:
			afterRedactedEnd = false
		}
	}
	return true
}

// hbaRuleRedacted checks one line holding ldapbindpasswd text, and reports whether the line ends
// redacted, after which lines that don't start a rule must be too.
func hbaRuleRedacted(line string) (ok, endsRedacted bool) {
	tokens := tokenizeHba(line)
	var options []hbaToken
	for k, token := range tokens.tokens {
		if !strings.HasPrefix(token.text, hbaOptionPrefix) {
			continue
		}
		if token.text[len(hbaOptionPrefix):] != SecretReplacement ||
			!onlyHbaOptionsFollow(tokens.tokens, k) {
			return false, false
		}
		options = append(options, token)
		endsRedacted = token.end == len(line)
	}
	if len(options) > 0 && tokens.commentStart >= 0 {
		if line[tokens.commentStart+1:] != SecretReplacement {
			return false, false
		}
		endsRedacted = true
	}
	for from := indexOfHbaKey(line, 0, false); from >= 0; {
		nameEnd := matchHbaKey(line, from, false)
		if !insideHbaToken(options, from) {
			if line[hbaKeptEnd(line, nameEnd):] != SecretReplacement {
				return false, false
			}
			endsRedacted = true
		}
		from = indexOfHbaKey(line, nameEnd, false)
	}
	return true, endsRedacted
}
