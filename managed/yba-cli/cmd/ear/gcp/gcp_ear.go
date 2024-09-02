/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// GCPEARCmd represents the ear command
var GCPEARCmd = &cobra.Command{
	Use:     "gcp",
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere GCP encryption at rest (EAR) configuration",
	Long:    "Manage a GCP encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	GCPEARCmd.Flags().SortFlags = false

	// GCPEARCmd.AddCommand(createGCPEARCmd)
	// GCPEARCmd.AddCommand(updateGCPEARCmd)
	GCPEARCmd.AddCommand(listGCPEARCmd)
	GCPEARCmd.AddCommand(describeGCPEARCmd)
	GCPEARCmd.AddCommand(deleteGCPEARCmd)

	GCPEARCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))
}
