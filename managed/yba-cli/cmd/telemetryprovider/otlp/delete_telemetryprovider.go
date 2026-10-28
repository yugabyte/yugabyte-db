/*
 * Copyright (c) YugabyteDB, Inc.
 */

package otlp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteOTLPTelemetryProviderCmd represents the telemetry provider command
var deleteOTLPTelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete an OTLP YugabyteDB Anywhere telemetry provider",
	Long:    "Delete an OTLP telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetry-provider otlp delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(
			cmd,
			"OTLP",
			util.OTLPTelemetryProviderType,
		)
	},
}

func init() {
	deleteOTLPTelemetryProviderCmd.Flags().SortFlags = false
	deleteOTLPTelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
