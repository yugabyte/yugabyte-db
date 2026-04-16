/*
 * Copyright (c) YugabyteDB, Inc.
 */

package datadog

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeDataDogTelemetryProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere DataDog telemetry provider",
	Long:    "Describe a DataDog telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider datadog describe --name <datadog-provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.DescribeTelemetryProviderUtil(
			cmd,
			"DataDog",
			util.DataDogTelemetryProviderType,
		)
	},
}

func init() {
	describeDataDogTelemetryProviderCmd.Flags().SortFlags = false
}
