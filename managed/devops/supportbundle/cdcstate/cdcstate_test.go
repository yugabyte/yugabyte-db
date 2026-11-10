// Copyright (c) YugabyteDB, Inc.

package cdcstate

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"testing"
	"time"
)

func TestSmartSplit(t *testing.T) {
	cases := []struct {
		name string
		in   string
		want []string
	}{
		{
			name: "plain fields",
			in:   `"a", "b", "c", <NULL>, "e"`,
			want: []string{`"a"`, `"b"`, `"c"`, `<NULL>`, `"e"`},
		},
		{
			name: "comma inside a quoted top-level field",
			in:   `"a,b", "c"`,
			want: []string{`"a,b"`, `"c"`},
		},
		{
			name: "map is one field",
			in:   `"a", {string:"k1" -> string:"v1", string:"k2" -> string:"v2"}, "z"`,
			want: []string{`"a"`, `{string:"k1" -> string:"v1", string:"k2" -> string:"v2"}`, `"z"`},
		},
		{
			name: "open brace inside a map value",
			in:   `"a", {string:"k" -> string:"x{y"}, "z"`,
			want: []string{`"a"`, `{string:"k" -> string:"x{y"}`, `"z"`},
		},
		{
			name: "close brace inside a map value",
			in:   `"a", {string:"k" -> string:"x}y", string:"k2" -> string:"v2"}, "z"`,
			want: []string{`"a"`, `{string:"k" -> string:"x}y", string:"k2" -> string:"v2"}`, `"z"`},
		},
		{
			name: "close brace then escaped quote inside a map value",
			in:   `"a", {string:"k" -> string:"x}\"y", string:"k2" -> string:"v2"}, "z"`,
			want: []string{`"a"`, `{string:"k" -> string:"x}\"y", string:"k2" -> string:"v2"}`, `"z"`},
		},
		{
			// An escaped backslash must not also escape the quote after it.
			name: "escaped backslash right before the closing quote",
			in:   `"a\\", "b"`,
			want: []string{`"a\\"`, `"b"`},
		},
		{
			name: "unterminated quote keeps the rest of the line",
			in:   `"a", "b, c`,
			want: []string{`"a"`, `"b, c`},
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if got := smartSplit(tc.in); !reflect.DeepEqual(got, tc.want) {
				t.Errorf("smartSplit(%s)\n got  %q\n want %q", tc.in, got, tc.want)
			}
		})
	}
}

// A row as yb-ts-cli prints it, with the map in DocDB's sorted key order so that start_hash_range
// and xmin follow snapshot_key: a brace inside snapshot_key is a threat to exactly those columns
// and to last_replication_time.
func cdcStateRow(snapshotKey string) string {
	return `"tab1", "strm1", "1.23", {string:"active_time" -> string:"1700000000000", ` +
		`string:"snapshot_key" -> string:"` + snapshotKey + `", ` +
		`string:"start_hash_range" -> string:"0", string:"xmin" -> string:"42"}, "1700000000123"`
}

// During a CDCSDK snapshot, snapshot_key is the encoded DocKey the scan resumes from. Its
// non-printable bytes arrive as \xNN, but '{', '}' and '"' (escaped as \") arrive as themselves.
func TestParseRowSnapshotKeyBraces(t *testing.T) {
	cases := []struct{ name, key string }{
		{"no brace", `G\x12\x34S100\x00\x00!`},
		{"open brace byte", `G\x12{S100\x00\x00!`},
		{"close brace byte", `G\x12}S100\x00\x00!`},
		{"close brace then quote byte", `G}\"S100\x00\x00!`},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			row := parseRow(cdcStateRow(tc.key))
			want := map[string]string{
				"tablet_id":             "tab1",
				"stream_id":             "strm1",
				"checkpoint":            "1.23",
				"active_time":           "1700000000000",
				"snapshot_key":          decodeMapEscapes(tc.key),
				"start_hash_range":      "0",
				"xmin_val":              "42",
				"last_replication_time": "1700000000123",
			}
			for col, v := range want {
				if row[col] != v {
					t.Errorf("%s = %q, want %q", col, row[col], v)
				}
			}
		})
	}
}

func TestParseRowShapes(t *testing.T) {
	t.Run("null map still yields every column", func(t *testing.T) {
		row := parseRow(`"tab1", "strm1", "1.23", <NULL>, "5"`)
		if len(row) != len(ALLColumns) {
			t.Fatalf("got %d columns, want %d", len(row), len(ALLColumns))
		}
		if row["active_time"] != "" || row["last_replication_time"] != "5" {
			t.Errorf("active_time = %q, last_replication_time = %q", row["active_time"],
				row["last_replication_time"])
		}
	})
	t.Run("last_replication_time is optional", func(t *testing.T) {
		row := parseRow(`"tab1", "strm1", "1.23", {string:"xmin" -> string:"42"}`)
		if row["xmin_val"] != "42" || row["last_replication_time"] != "" {
			t.Errorf("xmin_val = %q, last_replication_time = %q", row["xmin_val"],
				row["last_replication_time"])
		}
	})
	t.Run("fewer than four fields is not a row", func(t *testing.T) {
		if row := parseRow(`"tab1", "strm1", "1.23"`); row != nil {
			t.Errorf("got %v, want nil", row)
		}
	})
}

// The tserver writes Read HT, then per table a blank line, Table Info and a blank line before
// Rows:, and at the end of the file a blank line, Row count:, XOR hash: and Hash scheme version:.
// The fixture puts Row count: straight after a row to cover that terminator as well.
func TestStreamDumpFile(t *testing.T) {
	dump := strings.Join([]string{
		`Read HT: { physical: 1700000000000000 }`,
		``,
		`Table Info:`,
		`decoy: "not", "a", "data", "row"`,
		``,
		`Rows:`,
		`"tab1", "strm1", "1.1", {string:"xmin" -> string:"7"}, <NULL>`,
		`"tab2", "strm2", "2.2", <NULL>, <NULL>`,
		``,
		`Table Info:`,
		`decoy: "not", "a", "data", "row"`,
		``,
		`Rows:`,
		`"tab3", "strm3", "3.3", <NULL>, <NULL>`,
		`Row count: 3`,
		`"tab4", "strm4", "4.4", <NULL>, <NULL>`,
	}, "\n") + "\n"
	path := filepath.Join(t.TempDir(), "dump.txt")
	if err := os.WriteFile(path, []byte(dump), 0o644); err != nil {
		t.Fatal(err)
	}
	var got []string
	err := streamDumpFile(path, func(row map[string]string) error {
		got = append(got, row["tablet_id"])
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	if want := []string{"tab1", "tab2", "tab3"}; !reflect.DeepEqual(got, want) {
		t.Errorf("tablet ids = %q, want %q", got, want)
	}

	// A failed write must stop the scan, not be dropped while later rows keep coming.
	calls := 0
	writeErr := errors.New("disk full")
	err = streamDumpFile(path, func(map[string]string) error {
		calls++
		return writeErr
	})
	if !errors.Is(err, writeErr) || calls != 1 {
		t.Errorf("err = %v after %d emit calls, want %v after 1", err, calls, writeErr)
	}
}

// fakeYbTsCli installs a yb-ts-cli that answers the two calls DumpToCSV makes. dump_tablet_data
// writes the tablet's dump to the destination path, as the tserver does: it copies <tablet>.dump
// from its own directory and fails for any tablet it has no dump for. list_tablets reports each of
// those tablets as a cdc_state peer, each after a user-table peer whose schema has a line longer
// than listTabletsLineBytes.
func fakeYbTsCli(t *testing.T, dumps map[string][]string) string {
	t.Helper()
	dir := t.TempDir()
	script := "#!/bin/bash\n" +
		`dir="$(dirname "$0")"` + "\n" +
		`echo "$*" >>"$dir/calls.log"` + "\n" +
		`if [ "${@: -1}" = list_tablets ]; then cat "$dir/list_tablets.out"; exit; fi` + "\n" +
		`src="$dir/${@: -2:1}.dump"` + "\n" +
		`[ -f "$src" ] || { echo "tablet not found" >&2; exit 1; }` + "\n" +
		`cp "$src" "${@: -1}"` + "\n"
	if err := os.WriteFile(filepath.Join(dir, "yb-ts-cli"), []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}
	var tablets []string
	for tablet, rows := range dumps {
		body := "Read HT: { physical: 1 }\n\nTable Info:\ntable_name: \"cdc_state\"\n\nRows:\n" +
			strings.Join(rows, "\n") + "\n"
		if err := os.WriteFile(filepath.Join(dir, tablet+".dump"), []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
		tablets = append(tablets, tablet)
	}
	sort.Strings(tablets)
	wide := "\t0:" + strings.Repeat("c", listTabletsLineBytes+1) + "[string NULLABLE VALUE]"
	var listing strings.Builder
	for _, tablet := range tablets {
		fmt.Fprintf(&listing, "Tablet id: user-%s\nState: RUNNING\nTable name: usertable\n"+
			"Schema: Schema [\n%s\n]\n", tablet, wide)
		fmt.Fprintf(&listing, "Tablet id: %s\nState: RUNNING\nTable name: cdc_state\n"+
			"Schema: Schema [\n\t0:tablet_id[string NOT NULL HASH]\n]\n", tablet)
	}
	out := filepath.Join(dir, "list_tablets.out")
	if err := os.WriteFile(out, []byte(listing.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	return dir
}

func dumpOptions(t *testing.T, binDir string, tabletIDs ...string) DumpOptions {
	work := t.TempDir()
	return DumpOptions{
		TserverAddress: "127.0.0.1:9100",
		Output:         filepath.Join(work, "cdc_state.csv"),
		DumpDir:        filepath.Join(work, "dump"),
		BinDir:         binDir,
		TabletIDs:      TabletIDs(tabletIDs),
		Stderr:         io.Discard,
	}
}

func TestDumpToCSV(t *testing.T) {
	dumps := map[string][]string{
		"tab1": {
			`"tab1", "strm1", "1.1", {string:"pub_refresh_times" -> string:"1,2", ` +
				`string:"xmin" -> string:"7"}, "100"`,
		},
		"tab2": {
			`"tab2", "strm2", "2.2", <NULL>, <NULL>`,
			`"tab2", "strm3", "2.3", {string:"active_time" -> string:"5"}, <NULL>`,
		},
	}
	// Byte for byte what the helper wrote when it collected every row before writing: streaming
	// must not change the CSV. Rows follow tablet order, and a value holding a comma is quoted.
	want := strings.Join(ALLColumns, ",") + "\n" +
		`tab1,strm1,1.1,,,,,,7,,,"1,2",,,,,100` + "\n" +
		`tab2,strm2,2.2,,,,,,,,,,,,,,` + "\n" +
		`tab2,strm3,2.3,5,,,,,,,,,,,,,` + "\n"
	// Discovery must find exactly the tablets an explicit list names, past the listing's
	// over-long schema lines and the user-table peers around them.
	for _, tc := range []struct {
		name      string
		tabletIDs []string
	}{
		{"explicit tablet ids", []string{"tab1", "tab2"}},
		{"discovered from list_tablets", nil},
	} {
		t.Run(tc.name, func(t *testing.T) {
			opts := dumpOptions(t, fakeYbTsCli(t, dumps), tc.tabletIDs...)
			if err := DumpToCSV(opts); err != nil {
				t.Fatal(err)
			}
			got, err := os.ReadFile(opts.Output)
			if err != nil {
				t.Fatal(err)
			}
			if string(got) != want {
				t.Errorf("CSV mismatch\n got:\n%s\nwant:\n%s", got, want)
			}
			if left, _ := os.ReadDir(opts.DumpDir); len(left) != 0 {
				t.Errorf("per-tablet dumps left in %s: %d", opts.DumpDir, len(left))
			}
		})
	}
}

// Both yb-ts-cli calls carry --certs_dir_name for a TLS tserver, and neither does otherwise.
func TestDumpToCSVPassesTheCertsDir(t *testing.T) {
	for _, certs := range []string{"", "/home/yugabyte/yugabyte-tls-config"} {
		binDir := fakeYbTsCli(t, map[string][]string{
			"tab1": {`"tab1", "strm1", "1.1", <NULL>, <NULL>`},
		})
		opts := dumpOptions(t, binDir)
		opts.CertsDirName = certs
		if err := DumpToCSV(opts); err != nil {
			t.Fatal(err)
		}
		calls, err := os.ReadFile(filepath.Join(binDir, "calls.log"))
		if err != nil {
			t.Fatal(err)
		}
		lines := strings.Split(strings.TrimSpace(string(calls)), "\n")
		if len(lines) != 2 {
			t.Fatalf("certs %q: %d yb-ts-cli calls, want list_tablets and one dump:\n%s",
				certs, len(lines), calls)
		}
		for _, line := range lines {
			got := strings.Contains(line, "--certs_dir_name=")
			if got != (certs != "") || (certs != "" && !strings.Contains(line, "--certs_dir_name="+certs)) {
				t.Errorf("certs %q: call %q", certs, line)
			}
		}
	}
}

// tab1's row is already in the CSV when the second tablet fails, so the run must remove the file
// rather than leave a truncated dump that looks complete.
func TestDumpToCSVRemovesPartialOutputOnFailure(t *testing.T) {
	binDir := fakeYbTsCli(t, map[string][]string{
		"tab1": {`"tab1", "strm1", "1.1", <NULL>, <NULL>`},
	})
	opts := dumpOptions(t, binDir, "tab1", "missing")
	if err := DumpToCSV(opts); err == nil {
		t.Fatal("DumpToCSV succeeded although a tablet could not be dumped")
	}
	if _, err := os.Stat(opts.Output); !os.IsNotExist(err) {
		t.Errorf("partial output left at %s (stat err: %v)", opts.Output, err)
	}
}

func TestParseListTablets(t *testing.T) {
	wide := "\t7:" + strings.Repeat("c", listTabletsLineBytes+1) + "[string NULLABLE VALUE]"
	listing := strings.Join([]string{
		"Tablet id: aaa",
		"State: RUNNING",
		"Table name: usertable",
		"Schema: Schema [",
		wide,
		"]",
		// A Table name: with no Tablet id: since the last pairing belongs to no tablet.
		"Table name: cdc_state",
		// An over-long line between the two must not break the pairing.
		"Tablet id: bbb",
		wide,
		"Table name: cdc_state",
		// The last line needs no trailing newline.
		"Tablet id: ccc",
		"Table name: cdc_state",
	}, "\n")
	got, err := parseListTablets(strings.NewReader(listing))
	if err != nil {
		t.Fatal(err)
	}
	if want := []string{"bbb", "ccc"}; !reflect.DeepEqual(got, want) {
		t.Errorf("tablet ids = %q, want %q", got, want)
	}
}

func TestRunCmdStreaming(t *testing.T) {
	drain := func(r io.Reader) error {
		_, err := io.Copy(io.Discard, r)
		return err
	}
	t.Run("stdout reaches consume", func(t *testing.T) {
		var got string
		cmd := []string{"sh", "-c", `printf 'a\nb\n'`}
		err := runCmdStreaming(cmd, 10, func(r io.Reader) error {
			b, err := io.ReadAll(r)
			got = string(b)
			return err
		})
		if err != nil || got != "a\nb\n" {
			t.Errorf("got %q, err %v", got, err)
		}
	})
	t.Run("non-zero exit reports stderr", func(t *testing.T) {
		err := runCmdStreaming([]string{"sh", "-c", "echo boom >&2; exit 3"}, 10, drain)
		if err == nil || !strings.Contains(err.Error(), "boom") {
			t.Errorf("err = %v, want it to carry the command's stderr", err)
		}
	})
	t.Run("timeout", func(t *testing.T) {
		start := time.Now()
		err := runCmdStreaming([]string{"sleep", "10"}, 1, drain)
		elapsed := time.Since(start)
		if err == nil || !strings.Contains(err.Error(), "timed out") || elapsed > 5*time.Second {
			t.Errorf("err = %v after %s, want a timeout after ~1s", err, elapsed)
		}
	})
	// A consumer that stops early must not leave the command blocked on a full pipe: its error has
	// to come back promptly, not after the run hangs until the timeout.
	t.Run("consume stops early with output still pending", func(t *testing.T) {
		stop := errors.New("stop reading")
		start := time.Now()
		err := runCmdStreaming([]string{"head", "-c", "1000000", "/dev/zero"}, 5,
			func(io.Reader) error { return stop })
		if !errors.Is(err, stop) || time.Since(start) > 3*time.Second {
			t.Errorf("err = %v after %s, want %v promptly", err, time.Since(start), stop)
		}
	})
}

// --tablet_ids behaves like pflag's StringSlice: repeatable, split on commas, blanks dropped. The
// standard library still accepts the single-dash and = forms.
func TestTabletIDsFlag(t *testing.T) {
	var ids TabletIDs
	fs := flag.NewFlagSet("test", flag.ContinueOnError)
	fs.Var(&ids, "tablet_ids", "")
	args := []string{"--tablet_ids", "a,b", "--tablet_ids=c", "-tablet_ids", " d , ,e,"}
	if err := fs.Parse(args); err != nil {
		t.Fatal(err)
	}
	if want := (TabletIDs{"a", "b", "c", "d", "e"}); !reflect.DeepEqual(ids, want) {
		t.Errorf("ids = %q, want %q", ids, want)
	}
	if got := ids.String(); got != "a,b,c,d,e" {
		t.Errorf("String() = %q", got)
	}
	var unset *TabletIDs
	if got := unset.String(); got != "" {
		t.Errorf("String() on a nil receiver = %q, want empty", got)
	}
}
