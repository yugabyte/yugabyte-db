/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
)

// DownloadHashicorpVaultEITCmd represents the download command
var DownloadHashicorpVaultEITCmd = &cobra.Command{
	Use: "download",
	Short: "Download a Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's certifciates",
	Long: "Download a Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's certificates",
	Run: func(cmd *cobra.Command, args []string) {

		cmd.Help()
	},
}

func init() {
	DownloadHashicorpVaultEITCmd.Flags().SortFlags = false

	DownloadHashicorpVaultEITCmd.AddCommand(downloadRootHashicorpVaultEITCmd)
	DownloadHashicorpVaultEITCmd.AddCommand(downloadClientHashicorpVaultEITCmd)
}
