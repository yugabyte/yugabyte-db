/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listGCPEARCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short: "List GCP YugabyteDB Anywhere Encryption In Transit" +
		" (EAR) configurations",
	Long: "List GCP YugabyteDB Anywhere Encryption In Transit" +
		" (EAR) configurations",
	Example: `yba ear gcp list`,
	Run: func(cmd *cobra.Command, args []string) {
		earutil.ListEARUtil(cmd, "GCP", util.GCPEARType)
	},
}

func init() {
	listGCPEARCmd.Flags().SortFlags = false
}
