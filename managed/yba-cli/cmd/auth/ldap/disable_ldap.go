/*
 * Copyright (c) YugaByte, Inc.
 */

package ldap

import (
	"github.com/spf13/cobra"
)

// disableLDAPCmd is used to disable LDAP authentication for YBA
var disableLDAPCmd = &cobra.Command{
	Use:     "disable",
	Aliases: []string{"delete"},
	Short:   "Disable LDAP authentication for YBA",
	Long:    "Disable LDAP authentication for YBA",
	Run: func(cmd *cobra.Command, args []string) {
		disableLDAP()
	},
}

func init() {
	disableLDAPCmd.Flags().SortFlags = false
}
