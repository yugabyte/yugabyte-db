/*
 * Copyright (c) YugaByte, Inc.
 */

package loki

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listLokiTelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List Loki YugabyteDB Anywhere telemetry providers",
	Long:    "List Loki YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetryprovider loki list`,
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.ListTelemetryProviderUtil(cmd, "Loki", util.LokiTelemetryProviderType)
	},
}

func init() {
	listLokiTelemetryProviderCmd.Flags().SortFlags = false
}
