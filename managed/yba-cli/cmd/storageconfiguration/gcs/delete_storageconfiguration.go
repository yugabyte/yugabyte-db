/*
 * Copyright (c) YugaByte, Inc.
 */

package gcs

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteGCSStorageConfigurationCmd represents the storage command
var deleteGCSStorageConfigurationCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a GCS YugabyteDB Anywhere storage configuration",
	Long:  "Delete a GCS storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationUtil(cmd, util.GCSStorageConfigType)
	},
}

func init() {
	deleteGCSStorageConfigurationCmd.Flags().SortFlags = false
	deleteGCSStorageConfigurationCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
