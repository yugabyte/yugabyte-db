/*
 * Copyright (c) YugaByte, Inc.
 */

package loki

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeLokiTelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a Loki YugabyteDB Anywhere telemetry provider",
	Long:    "Describe a Loki telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider loki describe --name <loki-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(
			cmd,
			"Loki",
			util.LokiTelemetryProviderType,
		)
	},
}

func init() {
	describeLokiTelemetryProviderCmd.Flags().SortFlags = false
}
