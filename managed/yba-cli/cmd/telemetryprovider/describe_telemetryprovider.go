/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryprovider

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
)

var describeTelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	GroupID: "action",
	Short:   "Describe a YugabyteDB Anywhere telemetry provider",
	Long:    "Describe a telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider describe --name <telemetry-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(cmd, "", "")
	},
}

func init() {
	describeTelemetryProviderCmd.Flags().SortFlags = false
	describeTelemetryProviderCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the telemetry provider to get details.")
	describeTelemetryProviderCmd.MarkFlagRequired("name")
}
