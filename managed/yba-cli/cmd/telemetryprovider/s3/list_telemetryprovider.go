/*
 * Copyright (c) YugabyteDB, Inc.
 */

package s3

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listS3TelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List S3 YugabyteDB Anywhere telemetry providers",
	Long:    "List S3 YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetry-provider s3 list`,
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.ListTelemetryProviderUtil(
			cmd,
			"S3",
			util.S3TelemetryProviderType,
		)
	},
}

func init() {
	listS3TelemetryProviderCmd.Flags().SortFlags = false
}
