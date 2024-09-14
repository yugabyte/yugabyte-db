/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeAzureEARCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an Azure YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Long:    "Describe an Azure YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARUtil(cmd, "Azure", util.AzureEARType)

	},
}

func init() {
	describeAzureEARCmd.Flags().SortFlags = false
}
