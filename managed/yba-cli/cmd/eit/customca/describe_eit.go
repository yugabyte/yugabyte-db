/*
 * Copyright (c) YugaByte, Inc.
 */

package customca

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeCustomCAEITCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a Custom CA YugabyteDB Anywhere Encryption In Transit (EIT) configuration",
	Long:    "Describe a Custom CA YugabyteDB Anywhere Encryption In Transit (EIT) configuration",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITUtil(cmd, "Custom CA", util.CustomCertHostPathCertificateType)

	},
}

func init() {
	describeCustomCAEITCmd.Flags().SortFlags = false
}
