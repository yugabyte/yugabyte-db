/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteOCIStorageConfigurationCmd represents the storage command
var deleteOCIStorageConfigurationCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete an OCI YugabyteDB Anywhere storage configuration",
	Long:    "Delete an OCI storage configuration in YugabyteDB Anywhere",
	Example: `yba storage-config oci delete --name <storage-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DeleteStorageConfigurationUtil(cmd, util.OCIStorageConfigType)
	},
}

func init() {
	deleteOCIStorageConfigurationCmd.Flags().SortFlags = false
	deleteOCIStorageConfigurationCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
