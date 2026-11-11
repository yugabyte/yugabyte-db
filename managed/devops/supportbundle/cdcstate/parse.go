// Copyright (c) YugabyteDB, Inc.

package cdcstate

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"regexp"
	"strconv"
	"strings"
)

// maxDumpLineBytes caps one dump row. 16 MiB is far above any observed cdc_state row. Because rows
// stream, the largest row sets the helper's peak memory: parsing and writing one takes about seven
// times its length, so a row near the cap needs ~120 MiB — more than the bundle cgroup's
// memory.high on a node with a few GB available, where the helper would stall on it.
const maxDumpLineBytes = 16 << 20

// mapEntryRE matches one `string:"k" -> string:"v"` entry of the inline map field. Captured text
// is still escaped; decodeMapEscapes decodes it.
var mapEntryRE = regexp.MustCompile(`string:"((?:[^"\\]|\\.)*)"\s*->\s*string:"((?:[^"\\]|\\.)*)"`)

// mapColumnToMapKey reverses mapKeyToColumn, translating a CSV column name back to the key
// yb-ts-cli actually emits.
func mapColumnToMapKey(col string) string {
	for k, v := range mapKeyToColumn {
		if v == col {
			return k
		}
	}
	return col
}

// smartSplit splits a dump row on its top-level commas: those outside every quoted string and
// outside the inline map, which therefore arrives as a single field for parseMap.
//
// Quotes are tracked at every brace depth and braces count only outside quotes, because a value
// can carry braces that are not structure. yb-ts-cli prints strings through FormatBytesAsStr,
// which escapes '"', '\' and non-printable bytes but passes '{', '}' and ',' through raw, and
// snapshot_key carries them in practice: during a CDCSDK snapshot it holds the encoded DocKey the
// scan resumes from, any byte of which can be 0x7B or 0x7D.
//
// That escaping is also what makes quote boundaries unambiguous: inside a quoted string a
// backslash always starts an escape and consumes the next rune, and an unescaped '"' always ends
// the string.
func smartSplit(s string) []string {
	var parts []string
	var buf strings.Builder
	inQuotes := false
	escaped := false
	braceDepth := 0
	for _, ch := range s {
		if escaped {
			escaped = false
			buf.WriteRune(ch)
			continue
		}
		switch ch {
		case '\\':
			escaped = inQuotes
			buf.WriteRune(ch)
		case '"':
			inQuotes = !inQuotes
			buf.WriteRune(ch)
		case '{':
			if !inQuotes {
				braceDepth++
			}
			buf.WriteRune(ch)
		case '}':
			if !inQuotes && braceDepth > 0 {
				braceDepth--
			}
			buf.WriteRune(ch)
		case ',':
			if !inQuotes && braceDepth == 0 {
				parts = append(parts, strings.TrimSpace(buf.String()))
				buf.Reset()
			} else {
				buf.WriteRune(ch)
			}
		default:
			buf.WriteRune(ch)
		}
	}
	if tail := strings.TrimSpace(buf.String()); tail != "" {
		parts = append(parts, tail)
	}
	return parts
}

// parseMap parses the inline map field into key/value pairs. An empty field or <NULL> yields an
// empty map, so a row missing the map still produces a full-width CSV record.
func parseMap(raw string) map[string]string {
	raw = strings.TrimSpace(raw)
	if raw == "" || raw == "<NULL>" {
		return map[string]string{}
	}
	if strings.HasPrefix(raw, "{") {
		raw = raw[1:]
	}
	if strings.HasSuffix(raw, "}") {
		raw = raw[:len(raw)-1]
	}
	out := map[string]string{}
	for _, m := range mapEntryRE.FindAllStringSubmatch(raw, -1) {
		out[decodeMapEscapes(m[1])] = decodeMapEscapes(m[2])
	}
	return out
}

// decodeMapEscapes turns the escape sequences yb-ts-cli emits inside a map entry into the bytes
// they stand for, so a value containing a quote does not reach the CSV as \" with the backslash
// still attached.
//
// FormatBytesAsStr emits exactly three escapes — \", \\ and \xNN for every non-printable byte —
// and strconv.Unquote accepts all of them. A binary value therefore reaches the CSV as raw bytes,
// NULs included; snapshot_key is one during a CDCSDK snapshot. Anything Unquote rejects falls back
// to the raw capture rather than losing the value.
func decodeMapEscapes(s string) string {
	if !strings.Contains(s, `\`) {
		return s
	}
	if v, err := strconv.Unquote(`"` + s + `"`); err == nil {
		return v
	}
	return s
}

// cleanValue strips one layer of surrounding quotes and maps the literal <NULL> to the empty
// string.
func cleanValue(v string) string {
	v = strings.TrimSpace(v)
	if v == "<NULL>" {
		return ""
	}
	if len(v) >= 2 && v[0] == '"' && v[len(v)-1] == '"' {
		return v[1 : len(v)-1]
	}
	return v
}

// parseRow converts one dump row into a column-keyed record.
//
// Positions are fixed by yb-ts-cli's output: 0 tablet_id, 1 stream_id, 2 checkpoint, 3 the inline
// map, 4 last_replication_time. A row with fewer than 4 fields is not a data row and is dropped.
// Position 4 is optional because older tserver builds omit it.
func parseRow(line string) map[string]string {
	line = strings.TrimSpace(line)
	if line == "" {
		return nil
	}
	parts := smartSplit(line)
	if len(parts) < 4 {
		return nil
	}
	row := map[string]string{
		"tablet_id":             cleanValue(parts[0]),
		"stream_id":             cleanValue(parts[1]),
		"checkpoint":            cleanValue(parts[2]),
		"last_replication_time": "",
	}
	if len(parts) > 4 {
		row["last_replication_time"] = cleanValue(parts[4])
	}
	mapData := parseMap(parts[3])
	for _, col := range MAPColumns {
		mapKey := mapColumnToMapKey(col)
		row[col] = mapData[mapKey]
	}
	return row
}

// streamDumpFile reads one yb-ts-cli dump file and hands each row to emit as soon as it is parsed,
// so memory holds one row however large the tablet is. It stops at the first error emit returns.
// See the package doc for the envelope it walks.
func streamDumpFile(filepath string, emit func(row map[string]string) error) error {
	f, err := os.Open(filepath)
	if err != nil {
		return err
	}
	defer f.Close()

	inRows := false
	scanner := bufio.NewScanner(f)
	// One dump row is one line, and a row carrying a large pub_refresh_times map exceeds
	// bufio.Scanner's default 64 KiB token limit. That surfaces as bufio.ErrTooLong and loses the
	// whole dump rather than one row, so raise the ceiling. Still bounded, so a corrupt file
	// cannot drive unbounded allocation.
	scanner.Buffer(make([]byte, 0, 64<<10), maxDumpLineBytes)
	for scanner.Scan() {
		line := strings.TrimRight(scanner.Text(), "\r")
		if line == "Rows:" {
			inRows = true
			continue
		}
		if !inRows {
			continue
		}
		if strings.HasPrefix(line, "Row count:") || strings.HasPrefix(line, "XOR hash:") {
			break
		}
		if strings.TrimSpace(line) == "" || strings.HasPrefix(line, "Table Info:") {
			inRows = false
			continue
		}
		if row := parseRow(line); row != nil {
			if err := emit(row); err != nil {
				return err
			}
		}
	}
	return scanner.Err()
}

// listTabletsLineBytes bounds the one line of list_tablets output held in memory at a time.
const listTabletsLineBytes = 64 << 10

// parseListTablets reads `yb-ts-cli list_tablets` output and returns the ids of the cdc_state
// tablets. The output reports "Tablet id:" and "Table name:" on separate lines, so the two are
// correlated across lines.
//
// It holds one line at a time because the output covers every tablet peer on the tserver, each
// with its full schema at one line per column: its size follows the node's tablet count and table
// widths, not cdc_state, and is paid even where there is no cdc_state at all. A line longer than
// listTabletsLineBytes is skipped rather than failing the dump — the two lines that identify a
// tablet are short, so a long line is always schema or partition text.
func parseListTablets(r io.Reader) ([]string, error) {
	br := bufio.NewReaderSize(r, listTabletsLineBytes)
	var tablets []string
	var currentID *string
	for {
		line, err := br.ReadSlice('\n')
		for err == bufio.ErrBufferFull {
			line = nil
			_, err = br.ReadSlice('\n')
		}
		stripped := strings.TrimSpace(string(line))
		if strings.HasPrefix(stripped, "Tablet id:") {
			id := strings.TrimSpace(strings.SplitN(stripped, ":", 2)[1])
			currentID = &id
		} else if strings.HasPrefix(stripped, "Table name:") && currentID != nil {
			name := strings.TrimSpace(strings.SplitN(stripped, ":", 2)[1])
			if name == "cdc_state" {
				tablets = append(tablets, *currentID)
			}
			currentID = nil
		}
		if err == io.EOF {
			return tablets, nil
		}
		if err != nil {
			return nil, err
		}
	}
}

// The CSV writer below is hand-rolled but need not be: unlike the input format, the output is
// ordinary RFC 4180 and encoding/csv covers it.
//
// Note that csv.Writer is not byte-identical to this: it also quotes a field that begins with a
// space or tab, and the field `\.` (which Postgres COPY reads as end-of-data). Those inputs are
// not reachable today because cleanValue trims each field, but the difference is real, and it
// errs toward more quoting rather than less — so a swap is a small behaviour change in the safe
// direction, not a no-op.
func rowCSVValues(row map[string]string) []string {
	vals := make([]string, len(ALLColumns))
	for i, col := range ALLColumns {
		vals[i] = row[col]
	}
	return vals
}

func escapeCSVField(s string) string {
	if strings.ContainsAny(s, ",\"\n\r") {
		return `"` + strings.ReplaceAll(s, `"`, `""`) + `"`
	}
	return s
}

func formatCSVRow(row map[string]string) string {
	vals := rowCSVValues(row)
	for i, v := range vals {
		vals[i] = escapeCSVField(v)
	}
	return strings.Join(vals, ",")
}

// csvWriter writes the CSV a row at a time, so producing it never requires holding the rows.
type csvWriter struct {
	f *os.File
	w *bufio.Writer
}

// createCSV creates outputPath and writes the header row.
func createCSV(outputPath string) (*csvWriter, error) {
	f, err := os.Create(outputPath)
	if err != nil {
		return nil, err
	}
	c := &csvWriter{f: f, w: bufio.NewWriter(f)}
	if _, err := fmt.Fprintln(c.w, strings.Join(ALLColumns, ",")); err != nil {
		_ = f.Close()
		os.Remove(outputPath)
		return nil, err
	}
	return c, nil
}

func (c *csvWriter) writeRow(row map[string]string) error {
	_, err := fmt.Fprintln(c.w, formatCSVRow(row))
	return err
}

// close flushes and closes the file. A flush error means buffered rows never reached the file, so
// it counts as a failed write.
func (c *csvWriter) close() error {
	flushErr := c.w.Flush()
	closeErr := c.f.Close()
	if flushErr != nil {
		return flushErr
	}
	return closeErr
}
