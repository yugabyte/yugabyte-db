/*
 * Copyright (c) YugaByte, Inc.
 */

package datadog

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listDataDogTelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List DataDog YugabyteDB Anywhere telemetry providers",
	Long:    "List DataDog YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetryprovider datadog list`,
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.ListTelemetryProviderUtil(
			cmd,
			"DataDog",
			util.DataDogTelemetryProviderType,
		)
	},
}

func init() {
	listDataDogTelemetryProviderCmd.Flags().SortFlags = false
}
