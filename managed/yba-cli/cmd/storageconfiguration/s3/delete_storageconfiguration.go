/*
 * Copyright (c) YugabyteDB, Inc.
 */

package s3

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteS3StorageConfigurationCmd represents the storage command
var deleteS3StorageConfigurationCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a S3 YugabyteDB Anywhere storage configuration",
	Long:    "Delete a S3 storage configuration in YugabyteDB Anywhere",
	Example: `yba storage-config s3 delete --name <storage-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationUtil(cmd, util.S3StorageConfigType)
	},
}

func init() {
	deleteS3StorageConfigurationCmd.Flags().SortFlags = false
	deleteS3StorageConfigurationCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
