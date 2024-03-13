/*
 * Copyright (c) YugaByte, Inc.
 */

package nfs

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listNFSStorageConfigurationCmd = &cobra.Command{
	Use:   "list",
	Short: "List NFS YugabyteDB Anywhere storage-configurations",
	Long:  "List NFS YugabyteDB Anywhere storage-configurations",
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.ListStorageConfigurationUtil(cmd, util.NFSStorageConfigType, util.NFSStorageConfigType)

	},
}

func init() {
	listNFSStorageConfigurationCmd.Flags().SortFlags = false
}
