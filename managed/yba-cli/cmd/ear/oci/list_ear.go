/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listOCIEARCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List OCI YugabyteDB Anywhere encryption at rest (EAR) configurations",
	Long:    "List OCI YugabyteDB Anywhere encryption at rest (EAR) configurations",
	Example: `yba ear oci list`,
	Run: func(cmd *cobra.Command, args []string) {
		earutil.ListEARUtil(cmd, "OCI", util.OCIEARType)
	},
}

func init() {
	listOCIEARCmd.Flags().SortFlags = false
}
