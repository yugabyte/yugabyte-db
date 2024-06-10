/*
 * Copyright (c) YugaByte, Inc.
 */

package gcs

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeGCSStorageConfigurationCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a GCS YugabyteDB Anywhere storage configuration",
	Long:    "Describe a GCS storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationUtil(cmd, util.GCSStorageConfigType)
	},
}

func init() {
	describeGCSStorageConfigurationCmd.Flags().SortFlags = false
}
