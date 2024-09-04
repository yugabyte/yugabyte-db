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

var listEARCmd = &cobra.Command{
	Use:     "list",
	GroupID: "action",
	Short:   "List YugabyteDB Anywhere Encryption at Rest Configurations",
	Long:    "List YugabyteDB Anywhere Encryption at Rest Configurations",
	Run: func(cmd *cobra.Command, args []string) {
		earCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		earutil.ListEARUtil(cmd, "", earCode)

	},
}

func init() {
	listEARCmd.Flags().SortFlags = false

	listEARCmd.Flags().StringP("name", "n", "", "[Optional] Name of the configuration.")
	listEARCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the configuration, defaults to list all configs. "+
			"Allowed values: aws, gcp, azu, hashicorp.")
}
