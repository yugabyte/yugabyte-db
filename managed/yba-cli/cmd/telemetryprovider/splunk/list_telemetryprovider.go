/*
 * Copyright (c) YugaByte, Inc.
 */

package splunk

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listSplunkTelemetryProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List Splunk YugabyteDB Anywhere telemetry providers",
	Long:    "List Splunk YugabyteDB Anywhere telemetry providers",
	Example: `yba telemetryprovider splunk list`,
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.ListTelemetryProviderUtil(
			cmd,
			"Splunk",
			util.SplunkTelemetryProviderType,
		)
	},
}

func init() {
	listSplunkTelemetryProviderCmd.Flags().SortFlags = false
}
