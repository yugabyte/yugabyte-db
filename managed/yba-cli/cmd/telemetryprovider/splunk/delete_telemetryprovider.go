/*
 * Copyright (c) YugaByte, Inc.
 */

package splunk

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteSplunkTelemetryProviderCmd represents the telemetry provider command
var deleteSplunkTelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a Splunk YugabyteDB Anywhere telemetry provider",
	Long:    "Delete a Splunk telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider splunk delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(
			cmd,
			"Splunk",
			util.SplunkTelemetryProviderType,
		)
	},
}

func init() {
	deleteSplunkTelemetryProviderCmd.Flags().SortFlags = false
	deleteSplunkTelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
