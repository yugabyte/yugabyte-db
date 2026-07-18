/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ldap

import (
	"github.com/spf13/cobra"
)

// LdapCmd is used to manage LDAP configuration for YBA
var LdapCmd = &cobra.Command{
	Use:   "ldap",
	Short: "Configure LDAP authentication for YBA",
	Long:  "Configure LDAP authentication for YBA",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	LdapCmd.Flags().SortFlags = false
	LdapCmd.AddCommand(configureLDAPCmd)
	LdapCmd.AddCommand(disableLDAPCmd)
	LdapCmd.AddCommand(describeLDAPCmd)
}
