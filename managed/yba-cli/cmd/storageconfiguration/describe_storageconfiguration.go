/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
)

var describeStorageConfigurationCmd = &cobra.Command{
	Use:     "describe",
	GroupID: "action",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere storage configuration",
	Long:    "Describe a storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationUtil(cmd, "")
	},
}

func init() {
	describeStorageConfigurationCmd.Flags().SortFlags = false
	describeStorageConfigurationCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the storage configuration to get details.")
	describeStorageConfigurationCmd.MarkFlagRequired("name")
}
