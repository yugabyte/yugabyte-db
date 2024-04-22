/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/onprem/instancetypes"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/onprem/node"
)

// OnpremProviderCmd represents the provider command
var OnpremProviderCmd = &cobra.Command{
	Use:   "onprem",
	Short: "Setting up YugabyteDB Anywhere on-premises provider",
	Long: "Commands to control on-premises provider after creation " +
		"in YugabyteDB Anywhere. Manage the instance types and node " +
		"instance of a defined on-premises provider.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	OnpremProviderCmd.Flags().SortFlags = false
	OnpremProviderCmd.AddCommand(node.NodesCmd)
	OnpremProviderCmd.AddCommand(instancetypes.InstanceTypesCmd)

	OnpremProviderCmd.PersistentFlags().StringP("provider-name", "n", "",
		"[Required] The name of the on-premises provider for the corresponding operations.")
	OnpremProviderCmd.MarkPersistentFlagRequired("provider-name")
}
