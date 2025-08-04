/*
 * Copyright (c) YugaByte, Inc.
 */

package datadog

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteDataDogTelemetryProviderCmd represents the telemetry provider command
var deleteDataDogTelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a DataDog YugabyteDB Anywhere telemetry provider",
	Long:    "Delete a DataDog telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider datadog delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(
			cmd,
			"DataDog",
			util.DataDogTelemetryProviderType,
		)
	},
}

func init() {
	deleteDataDogTelemetryProviderCmd.Flags().SortFlags = false
	deleteDataDogTelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
