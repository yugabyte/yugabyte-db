/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryconfig

import (
	"github.com/spf13/cobra"
)

// UpgradeExportTelemetryConfigCmd represents the universe upgrade export-telemetry-configs
// command. The name matches the API resource it drives,
// POST/GET /customers/{cUUID}/universes/{uniUUID}/export-telemetry-configs.
var UpgradeExportTelemetryConfigCmd = &cobra.Command{
	Use:     "export-telemetry-configs",
	Aliases: []string{"export-telemetry-config", "telemetry-config"},
	Short:   "Manage telemetry export configuration for a YugabyteDB Anywhere Universe",
	Long: "Manage telemetry export configuration for a YugabyteDB Anywhere Universe. " +
		"Fetch the output of \"yba universe upgrade export-telemetry-configs get\", " +
		"make the required changes and submit the json input to " +
		"\"yba universe upgrade export-telemetry-configs set\". " +
		"YugabyteDB Anywhere stores one telemetry configuration per universe and the set " +
		"command replaces it wholesale, so any export type left out of the input is " +
		"disabled.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	UpgradeExportTelemetryConfigCmd.Flags().SortFlags = false

	UpgradeExportTelemetryConfigCmd.AddCommand(getTelemetryConfigCmd)
	UpgradeExportTelemetryConfigCmd.AddCommand(setTelemetryConfigCmd)
}
