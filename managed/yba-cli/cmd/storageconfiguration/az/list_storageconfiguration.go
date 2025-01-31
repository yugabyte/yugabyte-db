/*
 * Copyright (c) YugaByte, Inc.
 */

package azure

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listAZStorageConfigurationCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere storage-configurations",
	Long:    "List YugabyteDB Anywhere storage-configurations",
	Example: `yba storage-config azure list`,
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.ListStorageConfigurationUtil(cmd, "Azure", util.AzureStorageConfigType)

	},
}

func init() {
	listAZStorageConfigurationCmd.Flags().SortFlags = false
}
