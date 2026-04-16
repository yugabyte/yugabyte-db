/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
)

// DownloadCustomCAEITCmd represents the download command
var DownloadCustomCAEITCmd = &cobra.Command{
	Use: "download",
	Short: "Download a Custom CA YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's certifciates",
	Long: "Download a Custom CA YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's certificates",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	DownloadCustomCAEITCmd.Flags().SortFlags = false

	DownloadCustomCAEITCmd.AddCommand(downloadRootCustomCAEITCmd)
}
