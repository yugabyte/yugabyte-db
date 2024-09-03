/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listAzureEARCmd = &cobra.Command{
	Use: "list",
	Short: "List Azure YugabyteDB Anywhere Encryption In Transit" +
		" (EAR) configurations",
	Long: "List Azure YugabyteDB Anywhere Encryption In Transit" +
		" (EAR) configurations",
	Run: func(cmd *cobra.Command, args []string) {
		earutil.ListEARUtil(cmd, "Azure", util.AzureEARType)
	},
}

func init() {
	listAzureEARCmd.Flags().SortFlags = false
}
