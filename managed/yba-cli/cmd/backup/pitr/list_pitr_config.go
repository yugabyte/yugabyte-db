/*
 * Copyright (c) YugaByte, Inc.
 */

package pitr

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listPITRCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List PITR configurations for the universe",
	Long:    "List Point-In-Time Recovery (PITR) configurations for the universe",
	Example: `yba backup pitr list --universe-name <universe-name>`,
	Run: func(cmd *cobra.Command, args []string) {
		universeName := util.MustGetFlagString(cmd, "universe-name")
		ListPITRUtil(cmd, universeName)
	},
}

func init() {
	listPITRCmd.Flags().SortFlags = false
}
