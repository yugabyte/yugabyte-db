/*
 * Copyright (c) YugaByte, Inc.
 */

package gcpcloudmonitoring

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listGCPCloudMonitoringTelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List GCP Cloud Monitoring YugabyteDB Anywhere telemetry providers",
	Long:    "List GCP Cloud Monitoring YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetryprovider gcpcloudmonitoring list`,
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.ListTelemetryProviderUtil(
			cmd,
			"GCP Cloud Monitoring",
			util.GCPCloudMonitoringTelemetryProviderType,
		)
	},
}

func init() {
	listGCPCloudMonitoringTelemetryProviderCmd.Flags().SortFlags = false
}
