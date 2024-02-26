/*
 * Copyright (c) YugaByte, Inc.
 */

package update

import (
	"github.com/spf13/cobra"
)

// UpdateStorageConfigurationCmd represents the storange configuration command
var UpdateStorageConfigurationCmd = &cobra.Command{
	Use:   "update",
	Short: "Update a YugabyteDB Anywhere storage configuration",
	Long:  "Update a storage configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	UpdateStorageConfigurationCmd.Flags().SortFlags = false
	UpdateStorageConfigurationCmd.AddCommand(updateS3StorageConfigurationCmd)
	UpdateStorageConfigurationCmd.AddCommand(updateGCSStorageConfigurationCmd)
	UpdateStorageConfigurationCmd.AddCommand(updateAZStorageConfigurationCmd)
	UpdateStorageConfigurationCmd.AddCommand(updateNFSStorageConfigurationCmd)

	UpdateStorageConfigurationCmd.PersistentFlags().StringP("storage-config-name", "n", "",
		"[Required] The name of the storage configuration to be updated.")
	UpdateStorageConfigurationCmd.MarkPersistentFlagRequired("storage-config-name")

	UpdateStorageConfigurationCmd.PersistentFlags().String("update-storage-config-name", "",
		"[Optional] Update name of the storage configuration.")
}
