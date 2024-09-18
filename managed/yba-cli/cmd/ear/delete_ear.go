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

// deleteEARCmd represents the ear command
var deleteEARCmd = &cobra.Command{
	Use:     "delete",
	GroupID: "action",
	Short:   "Delete a YugabyteDB Anywhere encryption at rest configuration",
	Long:    "Delete an encryption at rest configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		earutil.DeleteEARUtil(cmd, "", earCode)
	},
}

func init() {
	deleteEARCmd.Flags().SortFlags = false
	deleteEARCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the configuration to be deleted.")
	deleteEARCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the configuration. "+
			"Allowed values: aws, gcp, azu, hashicorp.")
	deleteEARCmd.MarkFlagRequired("name")
	deleteEARCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
