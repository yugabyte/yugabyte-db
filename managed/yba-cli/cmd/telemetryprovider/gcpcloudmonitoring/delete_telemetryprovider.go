/*
 * Copyright (c) YugaByte, Inc.
 */

package gcpcloudmonitoring

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteGCPCloudMonitoringTelemetryProviderCmd represents the telemetry provider command
var deleteGCPCloudMonitoringTelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a GCP Cloud Monitoring YugabyteDB Anywhere telemetry provider",
	Long:    "Delete a GCP Cloud Monitoring telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider gcpcloudmonitoring delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(
			cmd,
			"GCP Cloud Monitoring",
			util.GCPCloudMonitoringTelemetryProviderType,
		)
	},
}

func init() {
	deleteGCPCloudMonitoringTelemetryProviderCmd.Flags().SortFlags = false
	deleteGCPCloudMonitoringTelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
