/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// AWSEARCmd represents the ear command
var AWSEARCmd = &cobra.Command{
	Use:     "aws",
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere AWS encryption at rest (EAR) configuration",
	Long:    "Manage an AWS encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	AWSEARCmd.Flags().SortFlags = false

	// AWSEARCmd.AddCommand(createAWSEARCmd)
	// AWSEARCmd.AddCommand(updateAWSEARCmd)
	AWSEARCmd.AddCommand(listAWSEARCmd)
	AWSEARCmd.AddCommand(describeAWSEARCmd)
	AWSEARCmd.AddCommand(deleteAWSEARCmd)

	AWSEARCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))
}
