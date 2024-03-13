/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// GCPProviderCmd represents the provider command
var GCPProviderCmd = &cobra.Command{
	Use:     util.GCPProviderType,
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere GCP provider",
	Long:    "Manage a GCP provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	GCPProviderCmd.Flags().SortFlags = false

	GCPProviderCmd.AddCommand(createGCPProviderCmd)
	// GCPProviderCmd.AddCommand(updateGCPProviderCmd)
	GCPProviderCmd.AddCommand(listGCPProviderCmd)
	GCPProviderCmd.AddCommand(describeGCPProviderCmd)
	GCPProviderCmd.AddCommand(deleteGCPProviderCmd)

	GCPProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
