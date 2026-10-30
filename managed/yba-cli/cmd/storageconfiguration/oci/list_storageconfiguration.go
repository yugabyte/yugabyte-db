/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listOCIStorageConfigurationCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List OCI YugabyteDB Anywhere storage-configurations",
	Long:    "List OCI YugabyteDB Anywhere storage-configurations",
	Example: `yba storage-config oci list`,
	Run: func(cmd *cobra.Command, args []string) {

		storageconfigurationutil.ListStorageConfigurationUtil(
			cmd,
			util.OCIStorageConfigType,
			util.OCIStorageConfigType,
		)

	},
}

func init() {
	listOCIStorageConfigurationCmd.Flags().SortFlags = false
}
