/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
)

// DownloadSelfSignedEITCmd represents the download command
var DownloadSelfSignedEITCmd = &cobra.Command{
	Use: "download",
	Short: "Download a Self Signed YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's certifciates",
	Long: "Download a Self Signed YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's certificates",
	Run: func(cmd *cobra.Command, args []string) {

		cmd.Help()
	},
}

func init() {
	DownloadSelfSignedEITCmd.Flags().SortFlags = false

	DownloadSelfSignedEITCmd.AddCommand(downloadRootSelfSignedEITCmd)
	DownloadSelfSignedEITCmd.AddCommand(downloadClientSelfSignedEITCmd)

}
