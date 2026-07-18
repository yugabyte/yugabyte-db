/*
 * Copyright (c) YugabyteDB, Inc.
 */

package splunk

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeSplunkTelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a Splunk YugabyteDB Anywhere telemetry provider",
	Long:    "Describe a Splunk telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider splunk describe --name <splunk-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(
			cmd,
			"Splunk",
			util.SplunkTelemetryProviderType,
		)
	},
}

func init() {
	describeSplunkTelemetryProviderCmd.Flags().SortFlags = false
}
