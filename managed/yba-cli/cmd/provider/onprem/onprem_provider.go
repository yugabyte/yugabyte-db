/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/onprem/instancetypes"
)

// OnpremProviderCmd represents the provider command
var OnpremProviderCmd = &cobra.Command{
	Use:   "onprem",
	Short: "Commands for YugabyteDB Anywhere on-premises provider",
	Long:  "Commands to control on-premises provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	OnpremProviderCmd.Flags().SortFlags = false
	// OnpremProviderCmd.AddCommand(nodes.NodesCmd)
	OnpremProviderCmd.AddCommand(instancetypes.InstanceTypesCmd)

	OnpremProviderCmd.PersistentFlags().StringP("provider-name", "n", "",
		"[Required] The name of the on-premises provider for the corresponding operations.")
	OnpremProviderCmd.MarkPersistentFlagRequired("provider-name")
}
