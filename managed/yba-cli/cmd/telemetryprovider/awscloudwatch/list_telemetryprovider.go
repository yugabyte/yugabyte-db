/*
 * Copyright (c) YugaByte, Inc.
 */

package awscloudwatch

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listAWSCloudWatchTelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List AWS CloudWatch YugabyteDB Anywhere telemetry providers",
	Long:    "List AWS CloudWatch YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetryprovider awscloudwatch list`,
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.ListTelemetryProviderUtil(
			cmd,
			"AWS CloudWatch",
			util.AWSCloudWatchTelemetryProviderType,
		)
	},
}

func init() {
	listAWSCloudWatchTelemetryProviderCmd.Flags().SortFlags = false
}
