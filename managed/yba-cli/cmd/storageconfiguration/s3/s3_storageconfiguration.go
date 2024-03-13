/*
 * Copyright (c) YugaByte, Inc.
 */

package s3

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// S3StorageConfigurationCmd represents the storange configuration command
var S3StorageConfigurationCmd = &cobra.Command{
	Use:     "s3",
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere S3 storage configuration",
	Long:    "Manage a S3 storage configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	S3StorageConfigurationCmd.Flags().SortFlags = false
	S3StorageConfigurationCmd.AddCommand(createS3StorageConfigurationCmd)
	S3StorageConfigurationCmd.AddCommand(updateS3StorageConfigurationCmd)
	S3StorageConfigurationCmd.AddCommand(listS3StorageConfigurationCmd)
	S3StorageConfigurationCmd.AddCommand(describeS3StorageConfigurationCmd)
	S3StorageConfigurationCmd.AddCommand(deleteS3StorageConfigurationCmd)

	S3StorageConfigurationCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the storage configuration for the operation. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))

}
