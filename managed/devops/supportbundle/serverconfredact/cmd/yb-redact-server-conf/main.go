// Copyright (c) YugabyteDB, Inc.

// Command yb-redact-server-conf rewrites a server.conf in place with its credentials replaced.
//
// It runs on the DB node, invoked by redact_server_conf_in_staging in devops/bin/node_utils.sh,
// so that secrets are removed before the support bundle leaves the node.
//
// Exit codes are part of the contract with that script: 0 redacted, 1 I/O or decode failure,
// 2 usage error. The script maps any non-zero exit to REDACTION_STATUS=SKIPPED and still exits 0,
// which makes YBA redact the file itself — a node that predates this binary, or one where it
// fails, still produces a safe bundle rather than no bundle.
package main

import (
	"errors"
	"flag"
	"fmt"
	"os"

	"github.com/yugabyte/yugabyte-db/managed/devops/supportbundle/serverconfredact"
)

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	switch os.Args[1] {
	case "redact-file":
		os.Exit(runRedactFile(os.Args[2:]))
	// Pre-flight probe. node_utils.sh only checks the exit status here; printing the replacement
	// token also confirms the binary is linked against the expected package.
	case "smoke":
		fmt.Print(serverconfredact.SecretReplacement)
		os.Exit(0)
	default:
		usage()
		os.Exit(2)
	}
}

func usage() {
	fmt.Fprintf(os.Stderr, `usage:
  %s redact-file --in-place --file PATH [--yb-software-version VERSION] [--sensitive-gflags CSV] [--sensitive-gflags-b64 B64JSON]
  %s smoke
`, os.Args[0], os.Args[0])
}

func runRedactFile(args []string) int {
	fs := flag.NewFlagSet("redact-file", flag.ContinueOnError)
	inPlace := fs.Bool("in-place", false, "rewrite file in place")
	filePath := fs.String("file", "", "path to server.conf")
	ybVersion := fs.String("yb-software-version", "", "YB software version (informational)")
	sensitiveCSV := fs.String("sensitive-gflags", "", "comma-separated sensitive gflag names")
	sensitiveB64 := fs.String("sensitive-gflags-b64", "", "base64 JSON array of sensitive gflag names")
	if err := fs.Parse(args); err != nil {
		return 2
	}
	// Accepted but unused. YBA resolves the version-specific sensitive gflag list on its side and
	// passes the result in --sensitive-gflags-b64, so no version lookup happens on the node.
	// node_utils.sh passes the flag regardless; keeping it accepted avoids a usage error there.
	_ = ybVersion
	if *filePath == "" {
		fmt.Fprintln(os.Stderr, "--file is required")
		return 2
	}
	if !*inPlace {
		fmt.Fprintln(os.Stderr, "--in-place is required")
		return 2
	}
	content, err := os.ReadFile(*filePath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		return 1
	}
	var sensitive []string
	if *sensitiveB64 != "" {
		sensitive, err = serverconfredact.ParseSensitiveGflagsB64(*sensitiveB64)
		if err != nil {
			fmt.Fprintln(os.Stderr, err.Error())
			return 1
		}
	} else if *sensitiveCSV != "" {
		sensitive = serverconfredact.ParseSensitiveGflagsCSV(*sensitiveCSV)
	}
	redacted := serverconfredact.RedactSensitiveInfo(string(content), sensitive)
	if err := serverconfredact.WriteIfRedacted(*filePath, redacted, sensitive); err != nil {
		fmt.Fprintln(os.Stderr, "not writing "+*filePath+": "+err.Error())
		if errors.Is(err, serverconfredact.ErrUnredacted) {
			return 3
		}
		return 1
	}
	return 0
}
