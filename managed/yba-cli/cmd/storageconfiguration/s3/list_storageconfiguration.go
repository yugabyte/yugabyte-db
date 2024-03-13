/*
 * Copyright (c) YugaByte, Inc.
 */

package s3

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listS3StorageConfigurationCmd = &cobra.Command{
	Use:   "list",
	Short: "List S3 YugabyteDB Anywhere storage-configurations",
	Long:  "List S3 YugabyteDB Anywhere storage-configurations",
	Run: func(cmd *cobra.Command, args []string) {

		storageconfigurationutil.ListStorageConfigurationUtil(cmd, util.S3StorageConfigType, util.S3StorageConfigType)

	},
}

func init() {
	listS3StorageConfigurationCmd.Flags().SortFlags = false
}
