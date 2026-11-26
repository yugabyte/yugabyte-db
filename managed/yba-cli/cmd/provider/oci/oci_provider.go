/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/oci/instancetype"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// OCIProviderCmd represents the provider command
var OCIProviderCmd = &cobra.Command{
	Use:     util.OCIProviderType,
	Aliases: []string{"oracle"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere OCI provider",
	Long:    "Manage an OCI provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	OCIProviderCmd.Flags().SortFlags = false

	OCIProviderCmd.AddCommand(createOCIProviderCmd)
	OCIProviderCmd.AddCommand(updateOCIProviderCmd)
	OCIProviderCmd.AddCommand(listOCIProviderCmd)
	OCIProviderCmd.AddCommand(describeOCIProviderCmd)
	OCIProviderCmd.AddCommand(deleteOCIProviderCmd)
	OCIProviderCmd.AddCommand(instancetype.InstanceTypesCmd)

	OCIProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update, and some instance-type subcommands.",
				formatter.GreenColor,
			)))
}
