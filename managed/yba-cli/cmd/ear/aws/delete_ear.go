/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteAWSEARCmd represents the ear command
var deleteAWSEARCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere AWS encryption at rest configuration",
	Long:  "Delete an AWS encryption at rest configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARUtil(cmd, "AWS", util.AWSEARType)
	},
}

func init() {
	deleteAWSEARCmd.Flags().SortFlags = false
	deleteAWSEARCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
