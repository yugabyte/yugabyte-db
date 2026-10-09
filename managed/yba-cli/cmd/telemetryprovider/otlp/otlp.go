/*
 * Copyright (c) YugabyteDB, Inc.
 */

package otlp

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// OTLPTelemetryProviderCmd set of commands are used to perform operations on OTLP telemetry
// providers in YugabyteDB Anywhere
var OTLPTelemetryProviderCmd = &cobra.Command{
	Use:     "otlp",
	Aliases: []string{"opentelemetry"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere OTLP telemetry provider",
	Long:    "Manage an OTLP telemetry provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	OTLPTelemetryProviderCmd.AddCommand(createOTLPTelemetryProviderCmd)
	OTLPTelemetryProviderCmd.AddCommand(listOTLPTelemetryProviderCmd)
	OTLPTelemetryProviderCmd.AddCommand(describeOTLPTelemetryProviderCmd)
	OTLPTelemetryProviderCmd.AddCommand(deleteOTLPTelemetryProviderCmd)

	OTLPTelemetryProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
