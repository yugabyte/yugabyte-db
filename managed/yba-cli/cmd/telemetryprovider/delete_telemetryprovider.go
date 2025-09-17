/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryprovider

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
)

// deleteTelemetryProviderCmd represents the telemetry provider command
var deleteTelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	GroupID: "action",
	Short:   "Delete a YugabyteDB Anywhere telemetry provider",
	Long:    "Delete a telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(cmd, "", "")
	},
}

func init() {
	deleteTelemetryProviderCmd.Flags().SortFlags = false
	deleteTelemetryProviderCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the telemetry provider to be deleted.")
	deleteTelemetryProviderCmd.MarkFlagRequired("name")
	deleteTelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
