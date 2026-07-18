/*
 * Copyright (c) YugabyteDB, Inc.
 */

package awscloudwatch

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// AWSCloudWatchTelemetryProviderCmd set of commands are used to perform
// operations on AWS CloudWatch telemetry providers
// in YugabyteDB Anywhere
var AWSCloudWatchTelemetryProviderCmd = &cobra.Command{
	Use:     "awscloudwatch",
	Aliases: []string{"aws", "cloudwatch"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere AWS CloudWatch telemetry provider",
	Long:    "Manage an AWS CloudWatch telemetry provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	AWSCloudWatchTelemetryProviderCmd.AddCommand(createAWSCloudWatchTelemetryProviderCmd)
	AWSCloudWatchTelemetryProviderCmd.AddCommand(listAWSCloudWatchTelemetryProviderCmd)
	AWSCloudWatchTelemetryProviderCmd.AddCommand(describeAWSCloudWatchTelemetryProviderCmd)
	AWSCloudWatchTelemetryProviderCmd.AddCommand(deleteAWSCloudWatchTelemetryProviderCmd)

	AWSCloudWatchTelemetryProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
