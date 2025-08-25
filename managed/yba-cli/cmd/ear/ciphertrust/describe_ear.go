/*
 * Copyright (c) YugaByte, Inc.
 */

package ciphertrust

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeCipherTrustEARCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a CipherTrust YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Long:    "Describe a CipherTrust YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Example: `yba ear ciphertrust describe --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARUtil(cmd, "CipherTrust", util.CipherTrustEARType)
	},
}

func init() {
	describeCipherTrustEARCmd.Flags().SortFlags = false
}
