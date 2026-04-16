/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var downloadClientHashicorpVaultEITCmd = &cobra.Command{
	Use: "client",
	Short: "Download a Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's client certifciate",
	Long: "Download a Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's client certificate",
	Example: `yba eit hashicorp-vault download client --name <config-name> --username <username>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadEITValidation(cmd, "client")
	},
	Run: func(cmd *cobra.Command, args []string) {

		eitutil.DownloadClientEITUtil(cmd, "Hashicorp Vault", util.HashicorpVaultCertificateType)

	},
}

func init() {
	downloadClientHashicorpVaultEITCmd.Flags().SortFlags = false

	downloadClientHashicorpVaultEITCmd.Flags().String("username", "",
		"[Required] Connect to the database using this username for certificate-based authentication")

	downloadClientHashicorpVaultEITCmd.MarkFlagRequired("username")
}
