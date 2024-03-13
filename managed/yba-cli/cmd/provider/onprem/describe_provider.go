/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeOnpremProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an On-premises YugabyteDB Anywhere provider",
	Long:    "Describe an On-premises provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderUtil(cmd, "On-premises", util.OnpremProviderType)
	},
}

func init() {
	describeOnpremProviderCmd.Flags().SortFlags = false

}
