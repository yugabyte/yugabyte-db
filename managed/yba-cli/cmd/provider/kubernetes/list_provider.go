/*
 * Copyright (c) YugaByte, Inc.
 */

package kubernetes

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listK8sProviderCmd = &cobra.Command{
	Use:   "list",
	Short: "List Kubernetes YugabyteDB Anywhere providers",
	Long:  "List Kubernetes YugabyteDB Anywhere providers",
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "Kubernetes", util.K8sProviderType)
	},
}

func init() {
	listK8sProviderCmd.Flags().SortFlags = false
}
