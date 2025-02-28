/*
 * Copyright (c) YugaByte, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
)

// DownloadEITCmd represents the download command
var DownloadEITCmd = &cobra.Command{
	Use:     "download",
	GroupID: "action",
	Short: "Download YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's certifciates",
	Long: "Download YugabyteDB Anywhere Encryption In Transit (EIT) " +
		"configuration's certificate",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()

	},
}

func init() {
	DownloadEITCmd.Flags().SortFlags = false
	DownloadEITCmd.PersistentFlags().SortFlags = false

	DownloadEITCmd.AddCommand(downloadRootEITCmd)
	DownloadEITCmd.AddCommand(downloadClientEITCmd)

	DownloadEITCmd.PersistentFlags().
		StringP("name", "n", "", "[Required] Name of the configuration.")
	DownloadEITCmd.MarkPersistentFlagRequired("name")
	DownloadEITCmd.PersistentFlags().StringP("cert-type", "c", "",
		"[Optional] Type of the certificate. "+
			"Client certifcates cannot be downloaded for K8sCertManager or CustomCertHostPath. "+
			"Allowed values (case sensitive): SelfSigned, CustomCertHostPath, "+
			"HashicorpVault, K8sCertManager.")
}
