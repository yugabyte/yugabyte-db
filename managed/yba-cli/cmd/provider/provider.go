/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/create"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/onprem"
)

// ProviderCmd set of commands are used to perform operations on providers
// in YugabyteDB Anywhere
var ProviderCmd = &cobra.Command{
	Use:   "provider",
	Short: "Manage YugabyteDB Anywhere providers",
	Long:  "Manage YugabyteDB Anywhere providers",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	ProviderCmd.AddCommand(listProviderCmd)
	ProviderCmd.AddCommand(describeProviderCmd)
	ProviderCmd.AddCommand(deleteProviderCmd)
	ProviderCmd.AddCommand(create.CreateProviderCmd)
	ProviderCmd.AddCommand(onprem.OnpremProviderCmd)
}
