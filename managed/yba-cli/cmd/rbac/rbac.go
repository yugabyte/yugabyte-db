/*
 * Copyright (c) YugabyteDB, Inc.
 */

package rbac

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/permission"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/role"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/rolebinding"
)

// RBACCmd set of commands are used to manage RBAC in YugabyteDB Anywhere
var RBACCmd = &cobra.Command{
	Use:   "rbac",
	Short: "Manage YugabyteDB Anywhere RBAC (Role-Based Access Control)",
	Long:  "Manage YugabyteDB Anywhere RBAC (Role-Based Access Control)",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	RBACCmd.AddCommand(permission.PermissionCmd)
	RBACCmd.AddCommand(role.RoleCmd)
	RBACCmd.AddCommand(rolebinding.RoleBindingCmd)
}
