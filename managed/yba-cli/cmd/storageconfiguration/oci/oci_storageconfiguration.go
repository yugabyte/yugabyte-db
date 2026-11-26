/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// OCIStorageConfigurationCmd represents the storage configuration command
var OCIStorageConfigurationCmd = &cobra.Command{
	Use:     "oci",
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere OCI storage configuration",
	Long:    "Manage an OCI Object Storage configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	OCIStorageConfigurationCmd.Flags().SortFlags = false
	OCIStorageConfigurationCmd.AddCommand(createOCIStorageConfigurationCmd)
	OCIStorageConfigurationCmd.AddCommand(updateOCIStorageConfigurationCmd)
	OCIStorageConfigurationCmd.AddCommand(listOCIStorageConfigurationCmd)
	OCIStorageConfigurationCmd.AddCommand(describeOCIStorageConfigurationCmd)
	OCIStorageConfigurationCmd.AddCommand(deleteOCIStorageConfigurationCmd)

	OCIStorageConfigurationCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the storage configuration for the operation. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))

}
