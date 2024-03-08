/*
 * Copyright (c) YugaByte, Inc.
 */

package create

import (
	"github.com/spf13/cobra"
)

// CreateProviderCmd represents the provider command
var CreateProviderCmd = &cobra.Command{
	Use:   "create",
	Short: "Create a YugabyteDB Anywhere provider",
	Long:  "Create a provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	CreateProviderCmd.Flags().SortFlags = false
	CreateProviderCmd.AddCommand(createAWSProviderCmd)
	CreateProviderCmd.AddCommand(createGCPProviderCmd)
	CreateProviderCmd.AddCommand(createAzureProviderCmd)
	CreateProviderCmd.AddCommand(createK8sProviderCmd)
	CreateProviderCmd.AddCommand(createOnpremProviderCmd)

	CreateProviderCmd.PersistentFlags().StringP("provider-name", "n", "",
		"[Required] The name of the provider to be created.")
	CreateProviderCmd.MarkPersistentFlagRequired("provider-name")

}
