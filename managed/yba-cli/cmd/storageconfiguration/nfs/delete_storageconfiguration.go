/*
 * Copyright (c) YugaByte, Inc.
 */

package nfs

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteNFSStorageConfigurationCmd represents the storage command
var deleteNFSStorageConfigurationCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a NFS YugabyteDB Anywhere storage configuration",
	Long:  "Delete a NFS storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationUtil(cmd, util.NFSStorageConfigType)
	},
}

func init() {
	deleteNFSStorageConfigurationCmd.Flags().SortFlags = false
	deleteNFSStorageConfigurationCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
