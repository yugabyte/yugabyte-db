/*
 * Copyright (c) YugabyteDB, Inc.
 */

package role

import (
	"github.com/spf13/cobra"
)

// RoleCmd set of commands are used to manage Role in YugabyteDB Anywhere
var RoleCmd = &cobra.Command{
	Use:   "role",
	Short: "Manage YugabyteDB Anywhere RBAC roles",
	Long:  "Manage YugabyteDB Anywhere RBAC roles",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {

	RoleCmd.AddCommand(listRoleCmd)
	RoleCmd.AddCommand(describeRoleCmd)
	RoleCmd.AddCommand(deleteRoleCmd)
	RoleCmd.AddCommand(createRoleCmd)
	RoleCmd.AddCommand(updateRoleCmd)
}
