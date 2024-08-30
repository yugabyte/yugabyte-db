/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// AzureEARCmd represents the ear command
var AzureEARCmd = &cobra.Command{
	Use:     "azure",
	GroupID: "type",
	Aliases: []string{"az", "azu"},
	Short:   "Manage a YugabyteDB Anywhere Azure encryption at rest (EAR) configuration",
	Long:    "Manage an Azure encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	AzureEARCmd.Flags().SortFlags = false

	// AzureEARCmd.AddCommand(createAzureEARCmd)
	// AzureEARCmd.AddCommand(updateAzureEARCmd)
	AzureEARCmd.AddCommand(listAzureEARCmd)
	AzureEARCmd.AddCommand(describeAzureEARCmd)
	AzureEARCmd.AddCommand(deleteAzureEARCmd)

	AzureEARCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))
}
