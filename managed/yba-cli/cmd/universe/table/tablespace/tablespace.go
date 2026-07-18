/*
 * Copyright (c) YugabyteDB, Inc.
 */

package tablespace

import (
	"github.com/spf13/cobra"
)

// TablespaceCmd set of commands are used to perform operations on universes
// in YugabyteDB Anywhere
var TablespaceCmd = &cobra.Command{
	Use:   "tablespace",
	Short: "Manage YugabyteDB Anywhere universe tablespaces",
	Long:  "Manage YugabyteDB Anywhere universe tablespaces",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	TablespaceCmd.AddCommand(listTablespaceCmd)
	TablespaceCmd.AddCommand(describeTablespaceCmd)
}
