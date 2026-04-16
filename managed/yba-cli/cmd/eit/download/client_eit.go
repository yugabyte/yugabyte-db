/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var downloadClientEITCmd = &cobra.Command{
	Use: "client",
	Short: "Download YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's client certifciate.",
	Long: "Download YugabyteDB Anywhere Encryption In Transit (EIT) " +
		"configuration's client certificate. Cannot be used with certificate type " +
		"K8SCertManager or CustomCertHostPath.",
	Example: `yba eit download client --name <config-name> --username <username>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadEITValidation(cmd, "client")
	},
	Run: func(cmd *cobra.Command, args []string) {
		certType, err := cmd.Flags().GetString("cert-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		eitutil.DownloadClientEITUtil(cmd, "", certType)

	},
}

func init() {
	downloadClientEITCmd.Flags().SortFlags = false

	downloadClientEITCmd.Flags().String("username", "",
		"[Required] Connect to the database using this username for certificate-based authentication")

	downloadClientEITCmd.MarkFlagRequired("username")
}
