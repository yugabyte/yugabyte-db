/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listHashicorpVaultEITCmd = &cobra.Command{
	Use: "list",
	Short: "List Hashicorp Vault YugabyteDB Anywhere Encryption" +
		" In Transit (EIT) certificate configurations",
	Long: "List Hashicorp Vault YugabyteDB Anywhere Encryption" +
		" In Transit (EIT) certificate configurations",
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "Hashicorp Vault", util.HashicorpVaultCertificateType)
	},
}

func init() {
	listHashicorpVaultEITCmd.Flags().SortFlags = false
}
