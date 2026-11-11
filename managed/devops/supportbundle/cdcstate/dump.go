// Copyright (c) YugabyteDB, Inc.

package cdcstate

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// DumpOptions configures a cdc_state dump.
type DumpOptions struct {
	TserverAddress string    // required; local tserver to query
	Output         string    // CSV destination
	DumpDir        string    // scratch dir for per-tablet yb-ts-cli dumps, deleted as consumed
	BinDir         string    // explicit yb-ts-cli location, else FindYbTsCli searches
	TabletIDs      TabletIDs // tablets to dump; empty means discover the cdc_state tablets
	TsCliTimeoutMs int       // per yb-ts-cli invocation
	CertsDirName   string    // the node's TLS certs, for a tserver that refuses plaintext
	Stderr         io.Writer
}

// TabletIDs lists tablet ids and doubles as the --tablet_ids flag.Value: the flag may be repeated
// and each value is split on commas, so `--tablet_ids a,b --tablet_ids c` gives [a b c]. Blank
// entries are dropped, so a stray comma does not name an empty tablet.
type TabletIDs []string

func (t *TabletIDs) String() string {
	// The flag package may call String on a nil receiver.
	if t == nil {
		return ""
	}
	return strings.Join(*t, ",")
}

// Set appends the comma-separated ids in v.
func (t *TabletIDs) Set(v string) error {
	for _, id := range strings.Split(v, ",") {
		if id = strings.TrimSpace(id); id != "" {
			*t = append(*t, id)
		}
	}
	return nil
}

// DumpToCSV discovers the cdc_state tablets, dumps each one, and streams the parsed rows into a
// single CSV. On failure it removes the partial CSV, so opts.Output only ever holds a complete
// dump. Progress goes to opts.Stderr, where main also writes the error: YBA shows a command's
// stderr only when it fails, so a failure arrives with the progress that led up to it.
func DumpToCSV(opts DumpOptions) error {
	if opts.Stderr == nil {
		opts.Stderr = os.Stderr
	}
	if opts.Output == "" {
		opts.Output = "cdc_state_dump.csv"
	}
	if opts.DumpDir == "" {
		opts.DumpDir = "/tmp/cdc_state_dump"
	}
	if opts.TsCliTimeoutMs <= 0 {
		opts.TsCliTimeoutMs = 300000
	}

	ybTsCli, err := FindYbTsCli(opts.BinDir)
	if err != nil {
		return err
	}
	fmt.Fprintf(opts.Stderr, "yb-ts-cli : %s\n", ybTsCli)

	if err := os.MkdirAll(opts.DumpDir, 0o755); err != nil {
		return err
	}

	var tablets []string
	if len(opts.TabletIDs) > 0 {
		tablets = opts.TabletIDs
		fmt.Fprintf(opts.Stderr, "Using %d provided tablet ID(s)\n", len(tablets))
	} else {
		fmt.Fprintf(opts.Stderr, "Discovering cdc_state tablets on %s ...\n", opts.TserverAddress)
		tablets, err = ListCdcStateTablets(ybTsCli, opts.TserverAddress, opts.CertsDirName)
		if err != nil {
			return err
		}
		fmt.Fprintf(opts.Stderr, "Found %d cdc_state tablet(s)\n", len(tablets))
	}

	if len(tablets) == 0 {
		fmt.Fprintln(opts.Stderr, "No cdc_state tablets found — writing empty CSV.")
	}

	out, err := createCSV(opts.Output)
	if err != nil {
		return err
	}
	total, err := dumpTablets(opts, ybTsCli, tablets, out)
	if closeErr := out.close(); err == nil {
		err = closeErr
	}
	if err != nil {
		// Rows are written as they are parsed, so a failed run leaves a truncated CSV behind.
		os.Remove(opts.Output)
		return err
	}

	fmt.Fprintf(opts.Stderr, "\nTotal rows: %d\n", total)
	fmt.Fprintf(opts.Stderr, "CSV written to: %s\n", opts.Output)
	return nil
}

// dumpTablets dumps each tablet and streams its rows into out, returning how many rows it wrote.
func dumpTablets(opts DumpOptions, ybTsCli string, tablets []string, out *csvWriter) (int, error) {
	total := 0
	for idx, tabletID := range tablets {
		dest := filepath.Join(opts.DumpDir, fmt.Sprintf("cdc_dump_%s.txt", tabletID))
		fmt.Fprintf(opts.Stderr, "[%d/%d] dumping tablet %s\n", idx+1, len(tablets), tabletID)
		if err := DumpTabletData(
			ybTsCli, opts.TserverAddress, opts.CertsDirName, tabletID, dest, opts.TsCliTimeoutMs,
		); err != nil {
			return total, err
		}
		rows := 0
		err := streamDumpFile(dest, func(row map[string]string) error {
			rows++
			return out.writeRow(row)
		})
		if err != nil {
			return total, err
		}
		total += rows
		fmt.Fprintf(opts.Stderr, "         %d rows\n", rows)
		os.Remove(dest)
	}
	return total, nil
}
