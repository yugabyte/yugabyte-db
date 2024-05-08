/*
 * Copyright (c) YugaByte, Inc.
 */

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
