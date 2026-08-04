/*
 * Copyright (c) YugabyteDB, Inc.
 */

package group

import (
	"github.com/spf13/cobra"
)

// GroupsCmd set of commands are used to manage groups in YugabyteDB Anywhere
var GroupsCmd = &cobra.Command{
	Use:   "group",
	Short: "Manage YugabyteDB Anywhere groups",
	Long:  "Manage YugabyteDB Anywhere groups",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	GroupsCmd.Flags().SortFlags = false
	GroupsCmd.AddCommand(CreateGroupCmd)
	GroupsCmd.AddCommand(ListGroupMappingCmd)
	GroupsCmd.AddCommand(UpdateGroupCmd)
	GroupsCmd.AddCommand(DeleteGroupCmd)
}
