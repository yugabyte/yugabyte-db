/*
 * Copyright (c) YugabyteDB, Inc.
 */

package group

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// ListGroupMappingCmd executes the list groupMapping command
var ListGroupMappingCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere group mappings",
	Long:    "List YugabyteDB Anywhere group mappings",
	Example: "yba group list",
	Run: func(cmd *cobra.Command, args []string) {
		groupMappingName := util.MaybeGetFlagString(cmd, "name")
		authCode := util.MaybeGetFlagString(cmd, "auth-code")
		ListGroupMappingUtil(authCode, groupMappingName)
	},
}

func init() {
	ListGroupMappingCmd.Flags().SortFlags = false
	ListGroupMappingCmd.Flags().StringP("name", "n", "",
		"[Optional] Name of the group mapping. Use group name for OIDC and Group DN for LDAP.")
	ListGroupMappingCmd.Flags().StringP("auth-code", "c", "",
		"[Optional] Authentication code of the group mapping. Use LDAP or OIDC.")
}
