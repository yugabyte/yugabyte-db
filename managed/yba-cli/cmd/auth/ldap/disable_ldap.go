/*
 * Copyright (c) YugaByte, Inc.
 */

package ldap

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// disableLDAPCmd is used to disable LDAP authentication for YBA
var disableLDAPCmd = &cobra.Command{
	Use:     "disable",
	Aliases: []string{"delete"},
	Short:   "Disable LDAP authentication for YBA",
	Long:    "Disable LDAP authentication for YBA",
	Example: `yba ldap disable --reset-fields ldap-host,ldap-port`,
	Run: func(cmd *cobra.Command, args []string) {
		// Reset all LDAP fields to default values
		resetKeys := util.MaybeGetFlagStringSlice(cmd, "reset-fields")
		runtimeKeysToDelete := map[string]bool{
			util.ToggleLDAPKey: true,
		}
		for _, key := range resetKeys {
			if key == "ldap-ssl-protocol" {
				runtimeKeysToDelete[util.UseLDAPSKey] = true
				runtimeKeysToDelete[util.UseStartTLSKey] = true
			} else if runtimeKey, ok := util.GetLDAPKeyForResetFlag(key); ok {
				runtimeKeysToDelete[runtimeKey] = true
			} else {
				logrus.Warn(formatter.Colorize(
					"Unrecognized key: "+key+". Skipping it.\n", formatter.YellowColor))
			}
		}
		disableLDAP(util.MustGetFlagBool(cmd, "reset-all"), runtimeKeysToDelete)
	},
}

func init() {
	disableLDAPCmd.Flags().SortFlags = false
	disableLDAPCmd.Flags().Bool("reset-all", false,
		"[Optional] Reset all LDAP fields to default values")
	disableLDAPCmd.Flags().StringSlice("reset-fields", []string{},
		"[Optional] Reset specific LDAP fields to default values. "+
			"Comma separated list of fields. Example: --reset-fields <field1>,<field2>\n"+
			"           Allowed values: ldap-host, ldap-port, ldap-ssl-protocol,"+
			"tls-version, base-dn, dn-prefix, customer-uuid, search-and-bind-enabled, \n"+
			"                           search-attribute, search-filter, service-account-dn, default-role,"+
			"service-account-password, group-attribute, group-search-filter, \n"+
			"                           group-search-base, group-search-scope, group-use-query, group-use-role-mapping")
}
