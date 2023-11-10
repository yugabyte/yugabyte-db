/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"github.com/spf13/cobra"
)

// UniverseCmd set of commands are used to perform operations on universes
// in YugabyteDB Anywhere
var UniverseCmd = &cobra.Command{
	Use:   "universe",
	Short: "Manage YugabyteDB Anywhere universes",
	Long:  "Manage YugabyteDB Anywhere universes",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	UniverseCmd.AddCommand(listUniverseCmd)
	// UniverseCmd.AddCommand(describeUniverseCmd)
	UniverseCmd.AddCommand(deleteUniverseCmd)
	UniverseCmd.AddCommand(createUniverseCmd)
}
