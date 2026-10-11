/*
 * Copyright (c) YugabyteDB, Inc.
 */

package otlp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listOTLPTelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List OTLP YugabyteDB Anywhere telemetry providers",
	Long:    "List OTLP YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetry-provider otlp list`,
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.ListTelemetryProviderUtil(
			cmd,
			"OTLP",
			util.OTLPTelemetryProviderType,
		)
	},
}

func init() {
	listOTLPTelemetryProviderCmd.Flags().SortFlags = false
}
