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

var describeEITCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	GroupID: "action",
	Short:   "Describe a YugabyteDB Anywhere Encryption In Transit (EIT) configuration",
	Long:    "Describe a YugabyteDB Anywhere Encryption In Transit (EIT) configuration",
	Example: `yba eit describe --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		certType, err := cmd.Flags().GetString("cert-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		eitutil.DescribeEITUtil(cmd, "", certType)

	},
}

func init() {
	describeEITCmd.Flags().SortFlags = false

	describeEITCmd.Flags().StringP("name", "n", "", "[Required] Name of the configuration.")
	describeEITCmd.MarkFlagRequired("name")
	describeEITCmd.Flags().StringP("cert-type", "c", "",
		"[Optional] Type of the certificate. "+
			"Allowed values (case sensitive): SelfSigned, CustomCertHostPath, "+
			"HashicorpVault, K8sCertManager.")
}
