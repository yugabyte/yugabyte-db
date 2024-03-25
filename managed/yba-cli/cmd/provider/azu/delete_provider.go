/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteAzureProviderCmd represents the provider command
var deleteAzureProviderCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete an Azure YugabyteDB Anywhere provider",
	Long:  "Delete an Azure provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderUtil(cmd, "Azure", util.AzureProviderType)
	},
}

func init() {
	deleteAzureProviderCmd.Flags().SortFlags = false
	deleteAzureProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
