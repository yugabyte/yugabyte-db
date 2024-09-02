/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listHashicorpVaultEARCmd = &cobra.Command{
	Use: "list",
	Short: "List Hashicorp Vault YugabyteDB Anywhere Encryption In Transit" +
		" (EAR) configurations",
	Long: "List Hashicorp Vault YugabyteDB Anywhere Encryption In Transit" +
		" (EAR) configurations",
	Run: func(cmd *cobra.Command, args []string) {
		earutil.ListEARUtil(cmd, "Hashicorp Vault", util.HashicorpVaultEARType)
	},
}

func init() {
	listHashicorpVaultEARCmd.Flags().SortFlags = false
}
