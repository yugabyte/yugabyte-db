/*
 * Copyright (c) YugaByte, Inc.
 */

package create

import (
	"github.com/spf13/cobra"
)

// CreateStorageConfigurationCmd represents the storange configuration command
var CreateStorageConfigurationCmd = &cobra.Command{
	Use:   "create",
	Short: "Create a YugabyteDB Anywhere storage configuration",
	Long:  "Create a storage configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	CreateStorageConfigurationCmd.Flags().SortFlags = false
	CreateStorageConfigurationCmd.AddCommand(createS3StorageConfigurationCmd)
	CreateStorageConfigurationCmd.AddCommand(createGCSStorageConfigurationCmd)
	CreateStorageConfigurationCmd.AddCommand(createAZStorageConfigurationCmd)
	CreateStorageConfigurationCmd.AddCommand(createNFSStorageConfigurationCmd)

	CreateStorageConfigurationCmd.PersistentFlags().StringP("storage-config-name", "n", "",
		"[Required] The name of the storage configuration to be created.")
	CreateStorageConfigurationCmd.MarkPersistentFlagRequired("storage-config-name")

	CreateStorageConfigurationCmd.PersistentFlags().String("backup-location", "",
		"[Required] The complete backup location including \"s3://\", \"gs://\" or "+
			"\"https://<account-name>.blob.core.windows.net/<container-name>/<blob-name>\".")
	CreateStorageConfigurationCmd.MarkPersistentFlagRequired("backup-location")
}
