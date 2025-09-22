/*
 * Copyright (c) YugaByte, Inc.
 */

package gcpcloudmonitoring

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// GCPCloudMonitoringTelemetryProviderCmd set of commands are used
// to perform operations on GCP Cloud Monitoring telemetry providers
// in YugabyteDB Anywhere
var GCPCloudMonitoringTelemetryProviderCmd = &cobra.Command{
	Use:     "gcpcloudmonitoring",
	Aliases: []string{"gcp"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere GCP Cloud Monitoring telemetry provider",
	Long:    "Manage a GCP Cloud Monitoring telemetry provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	GCPCloudMonitoringTelemetryProviderCmd.AddCommand(listGCPCloudMonitoringTelemetryProviderCmd)
	GCPCloudMonitoringTelemetryProviderCmd.AddCommand(
		describeGCPCloudMonitoringTelemetryProviderCmd,
	)
	GCPCloudMonitoringTelemetryProviderCmd.AddCommand(deleteGCPCloudMonitoringTelemetryProviderCmd)
	// GCPCloudMonitoringTelemetryProviderCmd.AddCommand(createGCPCloudMonitoringTelemetryProviderCmd)
	GCPCloudMonitoringTelemetryProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
