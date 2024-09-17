/*
 * Copyright (c) YugaByte, Inc.
 */

package eit

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteEITCmd represents the eit command
var deleteEITCmd = &cobra.Command{
	Use:     "delete",
	GroupID: "action",
	Short:   "Delete a YugabyteDB Anywhere encryption in transit configuration",
	Long:    "Delete an encryption in transit configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitCertType, err := cmd.Flags().GetString("cert-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		eitutil.DeleteEITUtil(cmd, "", eitCertType)
	},
}

func init() {
	deleteEITCmd.Flags().SortFlags = false
	deleteEITCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the configuration to be deleted.")
	deleteEITCmd.Flags().StringP("cert-type", "c", "",
		"[Optional] Type of the certificate, defaults to list all configurations. "+
			"Allowed values: SelfSigned, CustomCertHostPath,"+
			" HashicorpVault, K8sCertManager.")
	deleteEITCmd.MarkFlagRequired("name")
	deleteEITCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
