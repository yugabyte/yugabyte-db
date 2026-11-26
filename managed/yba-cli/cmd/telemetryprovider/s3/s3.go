/*
 * Copyright (c) YugabyteDB, Inc.
 */

package s3

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// S3TelemetryProviderCmd set of commands are used to perform operations on S3 telemetry
// providers in YugabyteDB Anywhere
var S3TelemetryProviderCmd = &cobra.Command{
	Use:     "s3",
	Aliases: []string{"awss3"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere S3 telemetry provider",
	Long:    "Manage an S3 telemetry provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	S3TelemetryProviderCmd.AddCommand(createS3TelemetryProviderCmd)
	S3TelemetryProviderCmd.AddCommand(listS3TelemetryProviderCmd)
	S3TelemetryProviderCmd.AddCommand(describeS3TelemetryProviderCmd)
	S3TelemetryProviderCmd.AddCommand(deleteS3TelemetryProviderCmd)

	S3TelemetryProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
