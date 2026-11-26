/*
 * Copyright (c) YugabyteDB, Inc.
 */

package dynatrace

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteDynatraceTelemetryProviderCmd represents the telemetry provider command
var deleteDynatraceTelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a Dynatrace YugabyteDB Anywhere telemetry provider",
	Long:    "Delete a Dynatrace telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetry-provider dynatrace delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(
			cmd,
			"Dynatrace",
			util.DynatraceTelemetryProviderType,
		)
	},
}

func init() {
	deleteDynatraceTelemetryProviderCmd.Flags().SortFlags = false
	deleteDynatraceTelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
