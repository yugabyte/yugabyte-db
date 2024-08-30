/*
 * Copyright (c) YugaByte, Inc.
 */

package selfsigned

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeSelfSignedEITCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a Self Signed YugabyteDB Anywhere Encryption In Transit (EIT) configuration",
	Long:    "Describe a Self Signed YugabyteDB Anywhere Encryption In Transit (EIT) configuration",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITUtil(cmd, "Self Signed", util.SelfSignedCertificateType)

	},
}

func init() {
	describeSelfSignedEITCmd.Flags().SortFlags = false
}
