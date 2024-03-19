/*
 * Copyright (c) YugaByte, Inc.
 */

package gcs

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// GCSStorageConfigurationCmd represents the storange configuration command
var GCSStorageConfigurationCmd = &cobra.Command{
	Use:     "gcs",
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere GCS storage configuration",
	Long:    "Manage a GCS storage configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	GCSStorageConfigurationCmd.Flags().SortFlags = false
	GCSStorageConfigurationCmd.AddCommand(createGCSStorageConfigurationCmd)
	GCSStorageConfigurationCmd.AddCommand(updateGCSStorageConfigurationCmd)
	GCSStorageConfigurationCmd.AddCommand(listGCSStorageConfigurationCmd)
	GCSStorageConfigurationCmd.AddCommand(describeGCSStorageConfigurationCmd)
	GCSStorageConfigurationCmd.AddCommand(deleteGCSStorageConfigurationCmd)

	GCSStorageConfigurationCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the storage configuration for the operation. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))

}
