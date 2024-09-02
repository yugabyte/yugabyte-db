/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeHashicorpVaultEITCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short: "Describe a Hashicorp Vault YugabyteDB Anywhere " +
		"Encryption In Transit (EIT) configuration",
	Long: "Describe a Hashicorp Vault YugabyteDB Anywhere " +
		"Encryption In Transit (EIT) configuration",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITUtil(cmd, "Hashicorp Vault", util.HashicorpVaultCertificateType)

	},
}

func init() {
	describeHashicorpVaultEITCmd.Flags().SortFlags = false
}
