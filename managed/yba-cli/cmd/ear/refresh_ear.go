/*
 * Copyright (c) YugaByte, Inc.
 */

package ear

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var refreshEARCmd = &cobra.Command{
	Use:     "refresh",
	GroupID: "action",
	Short:   "Refresh a YugabyteDB Anywhere Encryption At Rest (EAR) configuration",
	Long:    "Refresh a YugabyteDB Anywhere Encryption At Rest (EAR) configuration",
	Example: `yba ear refresh --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		earutil.RefreshEARUtil(cmd, "", earCode)

	},
}

func init() {
	refreshEARCmd.Flags().SortFlags = false

	refreshEARCmd.Flags().StringP("name", "n", "", "[Required] Name of the configuration.")
	refreshEARCmd.MarkFlagRequired("name")
	refreshEARCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the configuration. "+
			"Allowed values: aws, gcp, azu, hashicorp.")
}
