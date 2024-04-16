/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/onprem/instancetypes"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/onprem/node"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// OnpremProviderCmd represents the provider command
var OnpremProviderCmd = &cobra.Command{
	Use:     util.OnpremProviderType,
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere on-premises provider",
	Long: "Create and manage an on-premises provider, " +
		"instance types and node instances in YugabyteDB Anywhere.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	OnpremProviderCmd.Flags().SortFlags = false
	OnpremProviderCmd.AddCommand(node.NodesCmd)
	OnpremProviderCmd.AddCommand(createOnpremProviderCmd)
	OnpremProviderCmd.AddCommand(instancetypes.InstanceTypesCmd)
	OnpremProviderCmd.AddCommand(listOnpremProviderCmd)
	OnpremProviderCmd.AddCommand(describeOnpremProviderCmd)
	OnpremProviderCmd.AddCommand(updateOnpremProviderCmd)
	OnpremProviderCmd.AddCommand(deleteOnpremProviderCmd)

	OnpremProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, instance-types and nodes.",
				formatter.GreenColor)))
}
