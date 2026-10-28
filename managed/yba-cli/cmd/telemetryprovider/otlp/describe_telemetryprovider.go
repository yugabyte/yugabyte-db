/*
 * Copyright (c) YugabyteDB, Inc.
 */

package otlp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeOTLPTelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an OTLP YugabyteDB Anywhere telemetry provider",
	Long:    "Describe an OTLP telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetry-provider otlp describe --name <otlp-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(
			cmd,
			"OTLP",
			util.OTLPTelemetryProviderType,
		)
	},
}

func init() {
	describeOTLPTelemetryProviderCmd.Flags().SortFlags = false
}
