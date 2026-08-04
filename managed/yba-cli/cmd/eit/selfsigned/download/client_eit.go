/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var downloadClientSelfSignedEITCmd = &cobra.Command{
	Use: "client",
	Short: "Download a Self Signed YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's client certifciate",
	Long: "Download a Self Signed YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's client certificate",
	Example: `yba eit self-signed download client --name <config-name> --username <username>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadEITValidation(cmd, "client")
	},
	Run: func(cmd *cobra.Command, args []string) {

		eitutil.DownloadClientEITUtil(cmd, "Self Signed", util.SelfSignedCertificateType)

	},
}

func init() {
	downloadClientSelfSignedEITCmd.Flags().SortFlags = false

	downloadClientSelfSignedEITCmd.Flags().String("username", "",
		"[Required] Connect to the database using this username for certificate-based authentication")

	downloadClientSelfSignedEITCmd.MarkFlagRequired("username")

}
