/*
 * Copyright (c) YugabyteDB, Inc.
 */

package main

import (
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/fips"
)

var Version string

func main() {
	fips.VerifyFipsMode()
	ybaAuthClient.SetVersion(Version)
	cmd.Execute(Version)
}
