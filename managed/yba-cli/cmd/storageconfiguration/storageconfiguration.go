/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/create"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/update"
)

// StorageConfigurationCmd set of commands are used to perform operations on storage
// configurations in YugabyteDB Anywhere
var StorageConfigurationCmd = &cobra.Command{
	Use:   "storage-config",
	Short: "Manage YugabyteDB Anywhere storage configurations",
	Long:  "Manage YugabyteDB Anywhere storage configurations",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	StorageConfigurationCmd.AddCommand(listStorageConfigurationCmd)
	StorageConfigurationCmd.AddCommand(describeStorageConfigurationCmd)
	StorageConfigurationCmd.AddCommand(deleteStorageConfigurationCmd)
	StorageConfigurationCmd.AddCommand(create.CreateStorageConfigurationCmd)
	StorageConfigurationCmd.AddCommand(update.UpdateStorageConfigurationCmd)

}
