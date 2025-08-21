/*
 * Copyright (c) YugaByte, Inc.
 */

package awscloudwatch

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteAWSCloudWatchTelemetryProviderCmd represents the telemetry provider command
var deleteAWSCloudWatchTelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete an AWS CloudWatch YugabyteDB Anywhere telemetry provider",
	Long:    "Delete an AWS CloudWatch telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider awscloudwatch delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(
			cmd,
			"AWS CloudWatch",
			util.AWSCloudWatchTelemetryProviderType,
		)
	},
}

func init() {
	deleteAWSCloudWatchTelemetryProviderCmd.Flags().SortFlags = false
	deleteAWSCloudWatchTelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
