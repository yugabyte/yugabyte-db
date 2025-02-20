/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var refreshHashicorpVaultEARCmd = &cobra.Command{
	Use: "refresh",
	Short: "Refresh a Hashicorp Vault YugabyteDB Anywhere " +
		"Encryption In Transit (EAR) configuration",
	Long: "Refresh a Hashicorp Vault YugabyteDB Anywhere " +
		"Encryption In Transit (EAR) configuration",
	Example: `yba ear hashicorp-vault refresh --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARUtil(cmd, "Hashicorp Vault", util.HashicorpVaultEARType)

	},
}

func init() {
	refreshHashicorpVaultEARCmd.Flags().SortFlags = false
}
