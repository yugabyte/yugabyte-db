/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var refreshAzureEARCmd = &cobra.Command{
	Use:     "refresh",
	Short:   "Refresh an Azure YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Long:    "Refresh an Azure YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Example: `yba ear azure refresh --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARUtil(cmd, "Azure", util.AzureEARType)

	},
}

func init() {
	refreshAzureEARCmd.Flags().SortFlags = false
}
