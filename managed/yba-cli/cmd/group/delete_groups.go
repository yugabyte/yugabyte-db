/*
 * Copyright (c) YugaByte, Inc.
 */

package group

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// DeleteGroupCmd set of commands are used to delete groups in YugabyteDB Anywhere
var DeleteGroupCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove"},
	Short:   "Delete a group",
	Long:    "Delete a group for LDAP/OIDC",
	Example: "yba group delete -n <group-name>",
	Run: func(cmd *cobra.Command, args []string) {
		groupName := util.MustGetFlagString(cmd, "name")
		DeleteGroupUtil(cmd, groupName)
	},
}

func init() {
	DeleteGroupCmd.Flags().SortFlags = false
	DeleteGroupCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the group to be deleted")
	DeleteGroupCmd.MarkFlagRequired("name")
}
