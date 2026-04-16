/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ear

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var describeEARCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	GroupID: "action",
	Short:   "Describe a YugabyteDB Anywhere Encryption At Rest (EAR) configuration",
	Long:    "Describe a YugabyteDB Anywhere Encryption At Rest (EAR) configuration",
	Example: `yba ear describe --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		earutil.DescribeEARUtil(cmd, "", earCode)

	},
}

func init() {
	describeEARCmd.Flags().SortFlags = false

	describeEARCmd.Flags().StringP("name", "n", "", "[Required] Name of the configuration.")
	describeEARCmd.MarkFlagRequired("name")
	describeEARCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the configuration. "+
			"Allowed values: aws, gcp, azu, hashicorp.")
}
