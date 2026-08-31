/*
 * Copyright (c) YugabyteDB, Inc.
 */

package s3

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeS3TelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an S3 YugabyteDB Anywhere telemetry provider",
	Long:    "Describe an S3 telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetry-provider s3 describe --name <s3-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(
			cmd,
			"S3",
			util.S3TelemetryProviderType,
		)
	},
}

func init() {
	describeS3TelemetryProviderCmd.Flags().SortFlags = false
}
