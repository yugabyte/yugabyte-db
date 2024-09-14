/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteHashicorpVaultEITCmd represents the eit command
var deleteHashicorpVaultEITCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere Hashicorp Vault encryption in transit configuration",
	Long:  "Delete a Hashicorp Vault encryption in transit configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITUtil(cmd, "Hashicorp Vault", util.HashicorpVaultCertificateType)
	},
}

func init() {
	deleteHashicorpVaultEITCmd.Flags().SortFlags = false
	deleteHashicorpVaultEITCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
