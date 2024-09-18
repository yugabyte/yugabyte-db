/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteHashicorpVaultEARCmd represents the ear command
var deleteHashicorpVaultEARCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere Hashicorp Vault encryption at rest configuration",
	Long:  "Delete a Hashicorp Vault encryption at rest configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARUtil(cmd, "Hashicorp Vault", util.HashicorpVaultEARType)
	},
}

func init() {
	deleteHashicorpVaultEARCmd.Flags().SortFlags = false
	deleteHashicorpVaultEARCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
