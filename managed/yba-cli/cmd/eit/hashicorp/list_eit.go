/*
 * Copyright (c) YugabyteDB, Inc.
 */

package hashicorp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listHashicorpVaultEITCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short: "List Hashicorp Vault YugabyteDB Anywhere Encryption" +
		" In Transit (EIT) certificate configurations",
	Long: "List Hashicorp Vault YugabyteDB Anywhere Encryption" +
		" In Transit (EIT) certificate configurations",
	Example: `yba eit hashicorp-vault list`,
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.ListEITUtil(cmd, "Hashicorp Vault", util.HashicorpVaultCertificateType)
	},
}

func init() {
	listHashicorpVaultEITCmd.Flags().SortFlags = false
}
