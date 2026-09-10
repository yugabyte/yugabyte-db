/*
 * Copyright (c) YugabyteDB, Inc.
 */

package dynatrace

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listDynatraceTelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List Dynatrace YugabyteDB Anywhere telemetry providers",
	Long:    "List Dynatrace YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetry-provider dynatrace list`,
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.ListTelemetryProviderUtil(
			cmd,
			"Dynatrace",
			util.DynatraceTelemetryProviderType,
		)
	},
}

func init() {
	listDynatraceTelemetryProviderCmd.Flags().SortFlags = false
}
