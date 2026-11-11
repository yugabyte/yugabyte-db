/*
 * Copyright (c) YugabyteDB, Inc.
 */

package main

import (
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/cmd"
)

func main() {
	// FIPS mode is applied once the config is loaded, see initAfterFlagsParsed.
	cmd.Execute()

}
