/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeHashicorpVaultEARCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short: "Describe a Hashicorp Vault YugabyteDB Anywhere " +
		"Encryption In Transit (EAR) configuration",
	Long: "Describe a Hashicorp Vault YugabyteDB Anywhere " +
		"Encryption In Transit (EAR) configuration",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARUtil(cmd, "Hashicorp Vault", util.HashicorpVaultEARType)

	},
}

func init() {
	describeHashicorpVaultEARCmd.Flags().SortFlags = false
}
