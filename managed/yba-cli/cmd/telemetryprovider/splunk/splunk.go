/*
 * Copyright (c) YugabyteDB, Inc.
 */

package splunk

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// SplunkTelemetryProviderCmd set of commands are used to perform operations on Splunk telemetry providers
// in YugabyteDB Anywhere
var SplunkTelemetryProviderCmd = &cobra.Command{
	Use:     "splunk",
	Aliases: []string{"sp"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere Splunk telemetry provider",
	Long:    "Manage a Splunk telemetry provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	SplunkTelemetryProviderCmd.AddCommand(createSplunkTelemetryProviderCmd)
	SplunkTelemetryProviderCmd.AddCommand(listSplunkTelemetryProviderCmd)
	SplunkTelemetryProviderCmd.AddCommand(describeSplunkTelemetryProviderCmd)
	SplunkTelemetryProviderCmd.AddCommand(deleteSplunkTelemetryProviderCmd)

	SplunkTelemetryProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
