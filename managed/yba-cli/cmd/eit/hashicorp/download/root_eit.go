/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var downloadRootHashicorpVaultEITCmd = &cobra.Command{
	Use: "root",
	Short: "Download a Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's root certifciate",
	Long: "Download a Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's root certificate",
	Example: `yba eit hashicorp-vault download root --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadEITValidation(cmd, "root")
	},
	Run: func(cmd *cobra.Command, args []string) {

		eitutil.DownloadRootEITUtil(cmd, "Hashicorp Vault", util.HashicorpVaultCertificateType)

	},
}

func init() {
	downloadRootHashicorpVaultEITCmd.Flags().SortFlags = false
}
