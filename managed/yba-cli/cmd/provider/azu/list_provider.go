/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listAzureProviderCmd = &cobra.Command{
	Use:   "list",
	Short: "List Azure YugabyteDB Anywhere providers",
	Long:  "List Azure YugabyteDB Anywhere providers",
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "Azure", util.AzureProviderType)

	},
}

func init() {
	listAzureProviderCmd.Flags().SortFlags = false

}
