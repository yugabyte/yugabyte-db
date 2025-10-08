/*
 * Copyright (c) YugabyteDB, Inc.
 */

package datadog

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// DataDogTelemetryProviderCmd set of commands are used to perform operations on DataDog telemetry providers
// in YugabyteDB Anywhere
var DataDogTelemetryProviderCmd = &cobra.Command{
	Use:     "datadog",
	Aliases: []string{"dd"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere DataDog telemetry provider",
	Long:    "Manage a DataDog telemetry provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	DataDogTelemetryProviderCmd.AddCommand(createDataDogTelemetryProviderCmd)
	DataDogTelemetryProviderCmd.AddCommand(listDataDogTelemetryProviderCmd)
	DataDogTelemetryProviderCmd.AddCommand(describeDataDogTelemetryProviderCmd)
	DataDogTelemetryProviderCmd.AddCommand(deleteDataDogTelemetryProviderCmd)

	DataDogTelemetryProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
