/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteAzureEARCmd represents the ear command
var deleteAzureEARCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere Azure encryption at rest configuration",
	Long:  "Delete an Azure encryption at rest configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARUtil(cmd, "Azure", util.AzureEARType)
	},
}

func init() {
	deleteAzureEARCmd.Flags().SortFlags = false
	deleteAzureEARCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
