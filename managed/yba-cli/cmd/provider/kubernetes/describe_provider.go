/*
 * Copyright (c) YugaByte, Inc.
 */

package kubernetes

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeK8sProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a Kubernetes YugabyteDB Anywhere provider",
	Long:    "Describe a Kubernetes provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderUtil(cmd, "Kubernetes", util.K8sProviderType)
	},
}

func init() {
	describeK8sProviderCmd.Flags().SortFlags = false

}
