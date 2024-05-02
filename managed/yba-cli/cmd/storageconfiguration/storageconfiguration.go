/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

import (
	"github.com/spf13/cobra"
	azure "github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/az"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/gcs"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/nfs"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/s3"
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
	StorageConfigurationCmd.AddCommand(gcs.GCSStorageConfigurationCmd)
	StorageConfigurationCmd.AddCommand(azure.AZStorageConfigurationCmd)
	StorageConfigurationCmd.AddCommand(s3.S3StorageConfigurationCmd)
	StorageConfigurationCmd.AddCommand(nfs.NFSStorageConfigurationCmd)

	StorageConfigurationCmd.AddGroup(&cobra.Group{
		ID:    "action",
		Title: "Action Commands",
	})
	StorageConfigurationCmd.AddGroup(&cobra.Group{
		ID:    "type",
		Title: "Storage Configuration Type Commands",
	})

}
