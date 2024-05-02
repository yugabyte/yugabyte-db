/*
 * Copyright (c) YugaByte, Inc.
 */

package s3

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeS3StorageConfigurationCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a S3 YugabyteDB Anywhere storage configuration",
	Long:    "Describe a S3 storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationUtil(cmd, util.S3StorageConfigType)
	},
}

func init() {
	describeS3StorageConfigurationCmd.Flags().SortFlags = false

}
