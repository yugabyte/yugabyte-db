/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ldap

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// describeLDAPCmd is used to get LDAP authentication configuration for YBA
var describeLDAPCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"show", "get"},
	Short:   "Describe LDAP configuration for YBA",
	Long:    "Describe LDAP configuration for YBA",
	Example: `yba ldap describe`,
	Run: func(cmd *cobra.Command, args []string) {
		showInherited := !util.MustGetFlagBool(cmd, "user-set-only")
		getLDAPConfig(showInherited /*inherited*/)
	},
}

func init() {
	describeLDAPCmd.Flags().SortFlags = false
	describeLDAPCmd.Flags().Bool("user-set-only", false,
		"[Optional] Only show the fields that were set by the user explicitly.")
}
