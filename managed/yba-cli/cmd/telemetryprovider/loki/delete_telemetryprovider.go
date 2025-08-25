/*
 * Copyright (c) YugaByte, Inc.
 */

package loki

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteLokiTelemetryProviderCmd represents the telemetry provider command
var deleteLokiTelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a Loki YugabyteDB Anywhere telemetry provider",
	Long:    "Delete a Loki telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider loki delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(
			cmd,
			"Loki",
			util.LokiTelemetryProviderType,
		)
	},
}

func init() {
	deleteLokiTelemetryProviderCmd.Flags().SortFlags = false
	deleteLokiTelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
