/*
 * Copyright (c) YugabyteDB, Inc.
 */

package permission

import (
	"github.com/spf13/cobra"
)

// PermissionCmd set of commands are used to manage Permission in YugabyteDB Anywhere
var PermissionCmd = &cobra.Command{
	Use:   "permission",
	Short: "Manage YugabyteDB Anywhere RBAC permissions",
	Long:  "Manage YugabyteDB Anywhere RBAC permissions",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {

	PermissionCmd.AddCommand(listPermissionCmd)
	PermissionCmd.AddCommand(describePermissionCmd)
}
