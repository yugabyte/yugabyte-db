/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeAWSEARCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an AWS YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Long:    "Describe an AWS YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARUtil(cmd, "AWS", util.AWSEARType)

	},
}

func init() {
	describeAWSEARCmd.Flags().SortFlags = false
}
