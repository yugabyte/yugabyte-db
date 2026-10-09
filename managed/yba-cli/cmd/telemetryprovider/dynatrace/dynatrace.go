/*
 * Copyright (c) YugabyteDB, Inc.
 */

package dynatrace

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// DynatraceTelemetryProviderCmd set of commands are used to perform operations on Dynatrace
// telemetry providers in YugabyteDB Anywhere
var DynatraceTelemetryProviderCmd = &cobra.Command{
	Use:     "dynatrace",
	Aliases: []string{"dt"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere Dynatrace telemetry provider",
	Long:    "Manage a Dynatrace telemetry provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	DynatraceTelemetryProviderCmd.AddCommand(createDynatraceTelemetryProviderCmd)
	DynatraceTelemetryProviderCmd.AddCommand(listDynatraceTelemetryProviderCmd)
	DynatraceTelemetryProviderCmd.AddCommand(describeDynatraceTelemetryProviderCmd)
	DynatraceTelemetryProviderCmd.AddCommand(deleteDynatraceTelemetryProviderCmd)

	DynatraceTelemetryProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
