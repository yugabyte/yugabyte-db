/*
 * Copyright (c) YugabyteDB, Inc.
 */

package rolebinding

import (
	"github.com/spf13/cobra"
)

// RoleBindingCmd set of commands are used to manage RoleBindings in YugabyteDB Anywhere
var RoleBindingCmd = &cobra.Command{
	Use:   "role-binding",
	Short: "Manage YugabyteDB Anywhere RBAC role bindings",
	Long:  "Manage YugabyteDB Anywhere RBAC role bindings",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {

	RoleBindingCmd.AddCommand(listRoleBindingCmd)
	RoleBindingCmd.AddCommand(describeRoleBindingCmd)
	RoleBindingCmd.AddCommand(addRoleBindingCmd)
	RoleBindingCmd.AddCommand(deleteRoleBindingCmd)
}
