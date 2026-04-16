/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var downloadRootCustomCAEITCmd = &cobra.Command{
	Use: "root",
	Short: "Download a Custom CA YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's root certifciate",
	Long: "Download a Custom CA YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's root certificate",
	Example: `yba eit custom-ca download root --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadEITValidation(cmd, "root")
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadRootEITUtil(cmd, "Custom CA", util.CustomCertHostPathCertificateType)

	},
}

func init() {
	downloadRootCustomCAEITCmd.Flags().SortFlags = false
}
