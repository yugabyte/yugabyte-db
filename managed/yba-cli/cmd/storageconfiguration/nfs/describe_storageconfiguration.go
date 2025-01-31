/*
 * Copyright (c) YugaByte, Inc.
 */

package nfs

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeNFSStorageConfigurationCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a NFS YugabyteDB Anywhere storage configuration",
	Long:    "Describe a NFS storage configuration in YugabyteDB Anywhere",
	Example: `yba storage-config nfs describe --name <storage-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationUtil(cmd, util.NFSStorageConfigType)
	},
}

func init() {
	describeNFSStorageConfigurationCmd.Flags().SortFlags = false
}
