/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeAWSProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an AWS YugabyteDB Anywhere provider",
	Long:    "Describe an AWS provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderUtil(cmd, "AWS", util.AWSProviderType)
	},
}

func init() {
	describeAWSProviderCmd.Flags().SortFlags = false
}
