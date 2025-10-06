/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
)

// DownloadK8sCertManagerEITCmd represents the download command
var DownloadK8sCertManagerEITCmd = &cobra.Command{
	Use: "download",
	Short: "Download a K8s Cert Manager YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's certifciates",
	Long: "Download a K8s Cert Manager YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's certificates",
	Run: func(cmd *cobra.Command, args []string) {

		cmd.Help()
	},
}

func init() {
	DownloadK8sCertManagerEITCmd.Flags().SortFlags = false

	DownloadK8sCertManagerEITCmd.AddCommand(downloadRootK8sCertManagerEITCmd)
}
