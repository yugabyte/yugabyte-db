/*
 * Copyright (c) YugabyteDB, Inc.
 */

// FIPS mode off by default, though the binary is built with GOFIPS140: its crypto/tls rules
// (only RSA 2048/3072/4096 or ECDSA P-256/P-384 certs, no SHA-1) would reject certs a non-FIPS
// YBA serves, and the CLI cannot tell which kind of YBA it talks to. GODEBUG=fips140=on turns
// it back on.
//
//go:debug fips140=off
package main

import (
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
)

var Version string

func main() {
	ybaAuthClient.SetVersion(Version)
	cmd.Execute(Version)
}
