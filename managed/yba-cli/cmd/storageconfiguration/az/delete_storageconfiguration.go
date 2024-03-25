/*
 * Copyright (c) YugaByte, Inc.
 */

package azure

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
)

// deleteAZStorageConfigurationCmd represents the storage command
var deleteAZStorageConfigurationCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete an Azure YugabyteDB Anywhere storage configuration",
	Long:  "Delete an Azure storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationUtil(cmd, "Azure")
	},
}

func init() {
	deleteAZStorageConfigurationCmd.Flags().SortFlags = false

	deleteAZStorageConfigurationCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
