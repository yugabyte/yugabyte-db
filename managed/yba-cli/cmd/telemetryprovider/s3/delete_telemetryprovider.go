/*
 * Copyright (c) YugabyteDB, Inc.
 */

package s3

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteS3TelemetryProviderCmd represents the telemetry provider command
var deleteS3TelemetryProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete an S3 YugabyteDB Anywhere telemetry provider",
	Long:    "Delete an S3 telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetry-provider s3 delete --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DeleteTelemetryProviderUtil(
			cmd,
			"S3",
			util.S3TelemetryProviderType,
		)
	},
}

func init() {
	deleteS3TelemetryProviderCmd.Flags().SortFlags = false
	deleteS3TelemetryProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
