/*
 * Copyright (c) YugaByte, Inc.
 */

package customca

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/customca/download"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// CustomCAEITCmd represents the eit command
var CustomCAEITCmd = &cobra.Command{
	Use:     "custom-ca",
	Aliases: []string{"customca", "custom-cert-host-path"},
	GroupID: "type",
	Short: "Manage a YugabyteDB Anywhere Custom CA encryption" +
		" in transit (EIT) certificate configuration",
	Long: "Manage a Custom CA encryption in transit (EIT)" +
		" certificate configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	CustomCAEITCmd.Flags().SortFlags = false

	CustomCAEITCmd.AddCommand(createCustomCAEITCmd)
	CustomCAEITCmd.AddCommand(listCustomCAEITCmd)
	CustomCAEITCmd.AddCommand(describeCustomCAEITCmd)
	CustomCAEITCmd.AddCommand(deleteCustomCAEITCmd)
	CustomCAEITCmd.AddCommand(download.DownloadCustomCAEITCmd)

	CustomCAEITCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, download.",
				formatter.GreenColor)))
}
