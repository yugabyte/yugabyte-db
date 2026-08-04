/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var downloadRootSelfSignedEITCmd = &cobra.Command{
	Use: "root",
	Short: "Download a Self Signed YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's root certifciate",
	Long: "Download a Self Signed YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's root certificate",
	Example: `yba eit self-signed download root --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadEITValidation(cmd, "root")
	},
	Run: func(cmd *cobra.Command, args []string) {

		eitutil.DownloadRootEITUtil(cmd, "Self Signed", util.SelfSignedCertificateType)

	},
}

func init() {
	downloadRootSelfSignedEITCmd.Flags().SortFlags = false

}
