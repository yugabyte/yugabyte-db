/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
	"fmt"
	"os"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
)

func main() {
	b, err := os.ReadFile("version.txt")
	if err != nil {
		fmt.Print(err.Error() + "\n")
	}
	version := string(b)

	ybaAuthClient.SetVersion(version)
	cmd.Execute(version)
}
