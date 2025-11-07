/*
 * Copyright (c) YugabyteDB, Inc.
 */

package kubernetes

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listK8sProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List Kubernetes YugabyteDB Anywhere providers",
	Long:    "List Kubernetes YugabyteDB Anywhere providers",
	Example: `yba provider kubernetes list`,
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "Kubernetes", util.K8sProviderType)
	},
}

func init() {
	listK8sProviderCmd.Flags().SortFlags = false
}
