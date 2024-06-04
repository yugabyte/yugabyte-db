/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// AWSProviderCmd represents the provider command
var AWSProviderCmd = &cobra.Command{
	Use:     util.AWSProviderType,
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere AWS provider",
	Long:    "Manage an AWS provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	AWSProviderCmd.Flags().SortFlags = false

	AWSProviderCmd.AddCommand(createAWSProviderCmd)
	AWSProviderCmd.AddCommand(updateAWSProviderCmd)
	AWSProviderCmd.AddCommand(listAWSProviderCmd)
	AWSProviderCmd.AddCommand(describeAWSProviderCmd)
	AWSProviderCmd.AddCommand(deleteAWSProviderCmd)

	AWSProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))
}
