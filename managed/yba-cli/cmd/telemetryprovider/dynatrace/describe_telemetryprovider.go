/*
 * Copyright (c) YugabyteDB, Inc.
 */

package dynatrace

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeDynatraceTelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a Dynatrace YugabyteDB Anywhere telemetry provider",
	Long:    "Describe a Dynatrace telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetry-provider dynatrace describe --name <dynatrace-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(
			cmd,
			"Dynatrace",
			util.DynatraceTelemetryProviderType,
		)
	},
}

func init() {
	describeDynatraceTelemetryProviderCmd.Flags().SortFlags = false
}
