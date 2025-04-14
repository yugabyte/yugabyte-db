/*
 * Copyright (c) YugaByte, Inc.
 */

package gcs

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listGCSStorageConfigurationCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List GCS YugabyteDB Anywhere storage-configurations",
	Long:    "List GCS YugabyteDB Anywhere storage-configurations",
	Example: `yba storage-config gcs list`,
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.ListStorageConfigurationUtil(
			cmd,
			util.GCSStorageConfigType,
			util.GCSStorageConfigType,
		)

	},
}

func init() {
	listGCSStorageConfigurationCmd.Flags().SortFlags = false
}
