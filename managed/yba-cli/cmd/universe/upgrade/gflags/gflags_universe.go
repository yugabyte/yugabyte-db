/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gflags

import (
	"github.com/spf13/cobra"
)

// UpgradeGflagsCmd represents the universe upgrade gflags command
var UpgradeGflagsCmd = &cobra.Command{
	Use:   "gflags",
	Short: "Gflags upgrade for a YugabyteDB Anywhere Universe",
	Long: "Gflags upgrade for a YugabyteDB Anywhere Universe. " +
		"Fetch the output of \"yba universe upgrade gflags get\" command, " +
		"make required changes to the gflags and " +
		"submit the json input to \"yba universe upgrade gflags set\"",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	UpgradeGflagsCmd.Flags().SortFlags = false

	UpgradeGflagsCmd.AddCommand(setGflagsUniverseCmd)
	UpgradeGflagsCmd.AddCommand(getGflagsUniverseCmd)
}
