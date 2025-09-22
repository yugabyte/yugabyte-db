/*
 * Copyright (c) YugaByte, Inc.
 */

package loki

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// LokiTelemetryProviderCmd set of commands are used to perform operations on Loki telemetry providers
// in YugabyteDB Anywhere
var LokiTelemetryProviderCmd = &cobra.Command{
	Use:     "loki",
	Aliases: []string{"lo"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere Loki telemetry provider",
	Long:    "Manage a Loki telemetry provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	LokiTelemetryProviderCmd.AddCommand(listLokiTelemetryProviderCmd)
	LokiTelemetryProviderCmd.AddCommand(describeLokiTelemetryProviderCmd)
	LokiTelemetryProviderCmd.AddCommand(deleteLokiTelemetryProviderCmd)
	LokiTelemetryProviderCmd.AddCommand(createLokiTelemetryProviderCmd)

	LokiTelemetryProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
