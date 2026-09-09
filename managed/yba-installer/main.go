/*
 * Copyright (c) YugabyteDB, Inc.
 */

package main

import (
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/cmd"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

func main() {
	common.VerifyFipsMode()

	cmd.Execute()

}
