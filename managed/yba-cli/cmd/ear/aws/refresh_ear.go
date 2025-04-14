/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var refreshAWSEARCmd = &cobra.Command{
	Use:     "refresh",
	Short:   "Refresh an AWS YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Long:    "Refresh an AWS YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Example: `yba ear aws refresh --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARUtil(cmd, "AWS", util.AWSEARType)

	},
}

func init() {
	refreshAWSEARCmd.Flags().SortFlags = false
}
