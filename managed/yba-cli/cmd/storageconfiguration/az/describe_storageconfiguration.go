/*
 * Copyright (c) YugaByte, Inc.
 */

package azure

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
)

var describeAZStorageConfigurationCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an Azure YugabyteDB Anywhere storage configuration",
	Long:    "Describe an Azure storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationUtil(cmd, "Azure")
	},
}

func init() {
	describeAZStorageConfigurationCmd.Flags().SortFlags = false
}
