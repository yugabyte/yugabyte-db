/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcpcloudmonitoring

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeGCPCloudMonitoringTelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a GCP Cloud Monitoring YugabyteDB Anywhere telemetry provider",
	Long:    "Describe a GCP Cloud Monitoring telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider gcpcloudmonitoring describe --name <gcpcloudmonitoring-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(
			cmd,
			"GCP Cloud Monitoring",
			util.GCPCloudMonitoringTelemetryProviderType,
		)
	},
}

func init() {
	describeGCPCloudMonitoringTelemetryProviderCmd.Flags().SortFlags = false
}
