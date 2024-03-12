/*
 * Copyright (c) YugaByte, Inc.
 */

package kubernetes

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// K8sProviderCmd represents the provider command
var K8sProviderCmd = &cobra.Command{
	Use:        util.K8sProviderType,
	GroupID:    "type",
	Aliases:    []string{"k8s"},
	SuggestFor: []string{"gke", "eks", "aks"},
	Short:      "Manage a YugabyteDB Anywhere K8s provider",
	Long:       "Manage a K8s provider in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	K8sProviderCmd.Flags().SortFlags = false

	K8sProviderCmd.AddCommand(createK8sProviderCmd)
	// K8sProviderCmd.AddCommand(updateK8sProviderCmd)
	K8sProviderCmd.AddCommand(listK8sProviderCmd)
	K8sProviderCmd.AddCommand(describeK8sProviderCmd)
	K8sProviderCmd.AddCommand(deleteK8sProviderCmd)

	K8sProviderCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the provider for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe.",
				formatter.GreenColor)))
}
