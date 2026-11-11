// Copyright (c) YugabyteDB, Inc.

package main

import (
	"flag"
	"fmt"
	"os"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/devops/supportbundle/cdcstate"
)

func main() {
	if len(os.Args) >= 2 && os.Args[1] == "smoke" {
		fmt.Print("ok")
		os.Exit(0)
	}

	fs := flag.NewFlagSet("yb-dump-cdc-state", flag.ExitOnError)
	tserverAddress := fs.String("tserver_address", "", "Tserver RPC address, e.g. 10.0.0.1:9100")
	output := fs.String("output", "cdc_state_dump.csv", "Output CSV file")
	dumpDir := fs.String("dump_dir", "/tmp/cdc_state_dump", "Temp directory for dump files")
	binDir := fs.String("bin_dir", "", "Directory containing yb-ts-cli (auto-detected if omitted)")
	var tabletIDs cdcstate.TabletIDs
	fs.Var(&tabletIDs, "tablet_ids",
		"Tablet IDs to dump, comma-separated or repeated (auto-discovered if omitted)")
	tsCliTimeoutMs := fs.Int("ts_cli_timeout_ms", 300000, "Timeout for yb-ts-cli RPCs in ms")
	certsDirName := fs.String("certs_dir_name", "",
		"Directory with the node's TLS certificates, for a tserver that requires TLS")
	if err := fs.Parse(os.Args[1:]); err != nil {
		os.Exit(2)
	}
	if strings.TrimSpace(*tserverAddress) == "" {
		fmt.Fprintln(os.Stderr, "Error: --tserver_address is required")
		os.Exit(2)
	}

	err := cdcstate.DumpToCSV(cdcstate.DumpOptions{
		TserverAddress: *tserverAddress,
		Output:         *output,
		DumpDir:        *dumpDir,
		BinDir:         *binDir,
		TabletIDs:      tabletIDs,
		TsCliTimeoutMs: *tsCliTimeoutMs,
		CertsDirName:   *certsDirName,
		Stderr:         os.Stderr,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		os.Exit(1)
	}
}
