/*
 * Copyright (c) YugaByte, Inc.
 */

package kubernetes

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteK8sProviderCmd represents the provider command
var deleteK8sProviderCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a Kubernetes YugabyteDB Anywhere provider",
	Long:  "Delete a Kubernets provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderUtil(cmd, "Kubernetes", util.K8sProviderType)
	},
}

func init() {
	deleteK8sProviderCmd.Flags().SortFlags = false
	deleteK8sProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
