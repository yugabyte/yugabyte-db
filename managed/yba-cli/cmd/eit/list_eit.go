/*
 * Copyright (c) YugabyteDB, Inc.
 */

package eit

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var listEITCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	GroupID: "action",
	Short:   "List YugabyteDB Anywhere Encryption In Transit (EIT) configurations",
	Long:    "List YugabyteDB Anywhere Encryption In Transit (EIT) configurations",
	Example: `yba eit list`,
	Run: func(cmd *cobra.Command, args []string) {
		certType, err := cmd.Flags().GetString("cert-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		eitutil.ListEITUtil(cmd, "", certType)

	},
}

func init() {
	listEITCmd.Flags().SortFlags = false

	listEITCmd.Flags().StringP("name", "n", "", "[Optional] Name of the configuration.")
	listEITCmd.Flags().StringP("cert-type", "c", "",
		"[Optional] Type of the certificate, defaults to list all configurations. "+
			"Allowed values (case sensitive): SelfSigned, CustomCertHostPath, "+
			" HashicorpVault, K8sCertManager.")
}
