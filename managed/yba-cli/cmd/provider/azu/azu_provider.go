/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// AzureProviderCmd represents the provider command
var AzureProviderCmd = &cobra.Command{
	Use:     "azure",
	GroupID: "type",
	Aliases: []string{"az", util.AzureProviderType},
	Short:   "Manage a YugabyteDB Anywhere Azure provider",
	Long:    "Manage an Azure provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	AzureProviderCmd.Flags().SortFlags = false

	AzureProviderCmd.AddCommand(createAzureProviderCmd)
	// AzureProviderCmd.AddCommand(updateAzureProviderCmd)
	AzureProviderCmd.AddCommand(listAzureProviderCmd)
	AzureProviderCmd.AddCommand(describeAzureProviderCmd)
	AzureProviderCmd.AddCommand(deleteAzureProviderCmd)

	AzureProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
