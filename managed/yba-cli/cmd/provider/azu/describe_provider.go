/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeAzureProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an Azure YugabyteDB Anywhere provider",
	Long:    "Describe an Azure provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderUtil(cmd, "Azure", util.AzureProviderType)
	},
}

func init() {
	describeAzureProviderCmd.Flags().SortFlags = false
}
