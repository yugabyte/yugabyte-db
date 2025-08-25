/*
 * Copyright (c) YugaByte, Inc.
 */

package awscloudwatch

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeAWSCloudWatchTelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an AWS CloudWatch YugabyteDB Anywhere telemetry provider",
	Long:    "Describe an AWS CloudWatch telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider awscloudwatch describe --name <awscloudwatch-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(
			cmd,
			"AWS CloudWatch",
			util.AWSCloudWatchTelemetryProviderType,
		)
	},
}

func init() {
	describeAWSCloudWatchTelemetryProviderCmd.Flags().SortFlags = false
}
