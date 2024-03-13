/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/aws"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/azu"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/gcp"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/kubernetes"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/onprem"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// ProviderCmd set of commands are used to perform operations on providers
// in YugabyteDB Anywhere
var ProviderCmd = &cobra.Command{
	Use:   util.ProviderType,
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
	ProviderCmd.AddCommand(aws.AWSProviderCmd)
	ProviderCmd.AddCommand(azu.AzureProviderCmd)
	ProviderCmd.AddCommand(gcp.GCPProviderCmd)
	ProviderCmd.AddCommand(kubernetes.K8sProviderCmd)
	ProviderCmd.AddCommand(onprem.OnpremProviderCmd)

	ProviderCmd.AddGroup(&cobra.Group{
		ID:    "action",
		Title: "Action Commands",
	})
	ProviderCmd.AddGroup(&cobra.Group{
		ID:    "type",
		Title: "Provider Type Commands",
	})
}
