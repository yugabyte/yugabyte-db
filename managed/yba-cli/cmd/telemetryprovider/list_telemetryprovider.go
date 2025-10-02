/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryprovider

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var listTelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	GroupID: "action",
	Short:   "List YugabyteDB Anywhere telemetry providers",
	Long:    "List YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetryprovider list`,
	Run: func(cmd *cobra.Command, args []string) {
		providerType, err := cmd.Flags().GetString("type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		telemetryproviderutil.ListTelemetryProviderUtil(cmd, "", providerType)
	},
}

func init() {
	listTelemetryProviderCmd.Flags().SortFlags = false
	listTelemetryProviderCmd.Flags().
		StringP("name", "n", "", "[Optional] Name of the telemetry provider.")
	listTelemetryProviderCmd.Flags().StringP("type", "t", "",
		"[Optional] Type of the telemetry provider. "+
			"Allowed values: datadog, loki, splunk, awscloudwatch, gcpcloudmonitoring.")
}
