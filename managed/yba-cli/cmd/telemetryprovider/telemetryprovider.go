/*
 * Copyright (c) YugaByte, Inc.
 */

package telemetryprovider

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/awscloudwatch"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/datadog"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/gcpcloudmonitoring"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/loki"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/splunk"
)

// TelemetryProviderCmd set of commands are used to perform operations on telemetry providers
// in YugabyteDB Anywhere
var TelemetryProviderCmd = &cobra.Command{
	Use:     "telemetry-provider",
	Aliases: []string{"tp"},
	Short:   "Manage YugabyteDB Anywhere telemetry providers",
	Long:    "Manage YugabyteDB Anywhere telemetry providers for exporting logs and metrics via OTEL",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	TelemetryProviderCmd.AddCommand(listTelemetryProviderCmd)
	TelemetryProviderCmd.AddCommand(describeTelemetryProviderCmd)
	TelemetryProviderCmd.AddCommand(deleteTelemetryProviderCmd)
	// TelemetryProviderCmd.AddCommand(CreateTelemetryCmd)
	// TelemetryProviderCmd.AddCommand(updateTelemetryProviderCmd)
	TelemetryProviderCmd.AddCommand(datadog.DataDogTelemetryProviderCmd)
	TelemetryProviderCmd.AddCommand(splunk.SplunkTelemetryProviderCmd)
	TelemetryProviderCmd.AddCommand(awscloudwatch.AWSCloudWatchTelemetryProviderCmd)
	TelemetryProviderCmd.AddCommand(gcpcloudmonitoring.GCPCloudMonitoringTelemetryProviderCmd)
	TelemetryProviderCmd.AddCommand(loki.LokiTelemetryProviderCmd)

	TelemetryProviderCmd.AddGroup(&cobra.Group{
		ID:    "action",
		Title: "Action Commands",
	})
	TelemetryProviderCmd.AddGroup(&cobra.Group{
		ID:    "type",
		Title: "Telemetry Provider Type Commands",
	})
}
