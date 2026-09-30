/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// OCIEARCmd represents the ear command
var OCIEARCmd = &cobra.Command{
	Use:     "oci",
	Aliases: []string{"oracle"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere OCI encryption at rest (EAR) configuration",
	Long:    "Manage an OCI encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	OCIEARCmd.Flags().SortFlags = false

	OCIEARCmd.AddCommand(createOCIEARCmd)
	OCIEARCmd.AddCommand(updateOCIEARCmd)
	OCIEARCmd.AddCommand(listOCIEARCmd)
	OCIEARCmd.AddCommand(describeOCIEARCmd)
	OCIEARCmd.AddCommand(deleteOCIEARCmd)
	OCIEARCmd.AddCommand(refreshOCIEARCmd)

	OCIEARCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update and refresh.",
				formatter.GreenColor)))
}
