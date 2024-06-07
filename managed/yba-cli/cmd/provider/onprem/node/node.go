/*
 * Copyright (c) YugaByte, Inc.
 */

package node

import (
	"github.com/spf13/cobra"
)

// NodesCmd set of commands are used to perform operations on onprem nodes
// in YugabyteDB Anywhere
var NodesCmd = &cobra.Command{
	Use:   "node",
	Short: "Manage YugabyteDB Anywhere onprem node instances",
	Long:  "Manage YugabyteDB Anywhere on-premises node instances",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	NodesCmd.AddCommand(listNodesCmd)
	NodesCmd.AddCommand(removeNodesCmd)
	NodesCmd.AddCommand(addNodesCmd)
	NodesCmd.AddCommand(preflightNodesCmd)
}
