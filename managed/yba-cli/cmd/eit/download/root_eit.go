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

var downloadRootEITCmd = &cobra.Command{
	Use: "root",
	Short: "Download YugabyteDB Anywhere Encryption In Transit (EIT)" +
		" configuration's root certifciate",
	Long: "Download YugabyteDB Anywhere Encryption In Transit (EIT) " +
		"configuration's root certificate",
	Example: `yba eit download root --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadEITValidation(cmd, "root")
	},
	Run: func(cmd *cobra.Command, args []string) {
		certType, err := cmd.Flags().GetString("cert-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		eitutil.DownloadRootEITUtil(cmd, "", certType)

	},
}

func init() {
	downloadRootEITCmd.Flags().SortFlags = false
}
