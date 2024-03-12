/*
 * Copyright (c) YugaByte, Inc.
 */

package nfs

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// NFSStorageConfigurationCmd represents the storange configuration command
var NFSStorageConfigurationCmd = &cobra.Command{
	Use:     "nfs",
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere NFS storage configuration",
	Long:    "Manage a NFS storage configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	NFSStorageConfigurationCmd.Flags().SortFlags = false
	NFSStorageConfigurationCmd.AddCommand(createNFSStorageConfigurationCmd)
	NFSStorageConfigurationCmd.AddCommand(updateNFSStorageConfigurationCmd)
	NFSStorageConfigurationCmd.AddCommand(listNFSStorageConfigurationCmd)
	NFSStorageConfigurationCmd.AddCommand(describeNFSStorageConfigurationCmd)
	NFSStorageConfigurationCmd.AddCommand(deleteNFSStorageConfigurationCmd)

	NFSStorageConfigurationCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the storage configuration for the operation. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))

}
