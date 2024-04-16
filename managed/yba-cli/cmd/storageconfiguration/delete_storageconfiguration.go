/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
)

// deleteStorageConfigurationCmd represents the storage command
var deleteStorageConfigurationCmd = &cobra.Command{
	Use:     "delete",
	GroupID: "action",
	Short:   "Delete a YugabyteDB Anywhere storage configuration",
	Long:    "Delete a storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationUtil(cmd, "")
	},
}

func init() {
	deleteStorageConfigurationCmd.Flags().SortFlags = false
	deleteStorageConfigurationCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the storage configuration to be deleted.")
	deleteStorageConfigurationCmd.MarkFlagRequired("name")
	deleteStorageConfigurationCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
