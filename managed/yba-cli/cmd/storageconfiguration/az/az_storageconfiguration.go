/*
 * Copyright (c) YugaByte, Inc.
 */

package azure

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// AZStorageConfigurationCmd represents the storange configuration command
var AZStorageConfigurationCmd = &cobra.Command{
	Use:     "azure",
	GroupID: "type",
	Aliases: []string{"azu", "az"},
	Short:   "Manage a YugabyteDB Anywhere Azure storage configuration",
	Long:    "Manage an Azure storage configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	AZStorageConfigurationCmd.Flags().SortFlags = false
	AZStorageConfigurationCmd.AddCommand(createAZStorageConfigurationCmd)
	AZStorageConfigurationCmd.AddCommand(updateAZStorageConfigurationCmd)
	AZStorageConfigurationCmd.AddCommand(listAZStorageConfigurationCmd)
	AZStorageConfigurationCmd.AddCommand(describeAZStorageConfigurationCmd)
	AZStorageConfigurationCmd.AddCommand(deleteAZStorageConfigurationCmd)

	AZStorageConfigurationCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the storage configuration for the operation. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))

}
