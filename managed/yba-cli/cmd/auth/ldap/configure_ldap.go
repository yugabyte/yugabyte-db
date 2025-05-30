/*
 * Copyright (c) YugaByte, Inc.
 */

package ldap

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// configureLDAPCmd is used for configuring LDAP authentication for YBA
var configureLDAPCmd = &cobra.Command{
	Use:     "configure",
	Aliases: []string{"create", "enable"},
	Short:   "Configure LDAP authentication for YBA",
	Long:    "Configure/Update LDAP authentication for YBA",
	Example: `yba ldap configure --ldap-host <ldap-host> --ldap-port <ldap-port> --base-dn '"<base-dn>"'`,
	PreRun: func(cmd *cobra.Command, args []string) {
		// Validate the flags
		port := util.MustGetFlagInt64(cmd, "ldap-port")
		// check if port is a valid number
		if port < 0 || port > 65535 {
			logrus.Fatal(
				formatter.Colorize(
					"Port must be a valid number between 0 and 65535",
					formatter.RedColor,
				),
			)
		}
		ldapSSLProtocol := strings.ToLower(util.MaybeGetFlagString(cmd, "ldap-ssl-protocol"))
		if ldapSSLProtocol != "" && ldapSSLProtocol != util.LDAPWithSSL &&
			ldapSSLProtocol != util.LDAPWithStartTLS && ldapSSLProtocol != util.LDAPWithoutSSL {
			logrus.Fatal(
				formatter.Colorize(
					"Invalid value for --ldap-ssl-protocol. Possible values are none, ldaps and starttls.",
					formatter.RedColor,
				),
			)
		}
		ldapTLSVersion := util.MaybeGetFlagString(cmd, "ldap-tls-version")
		if ldapTLSVersion != "" && ldapTLSVersion != util.LdapTLSVersion1 &&
			ldapTLSVersion != util.LdapTLSVersion1_1 &&
			ldapTLSVersion != util.LdapTLSVersion1_2 {
			logrus.Fatal(
				formatter.Colorize(
					"Invalid value for --ldap-tls-version. Possible values are TLSv1, TLSv1_1 and TLSv1_2.",
					formatter.RedColor,
				),
			)
		}
		ldapSearchAttribute := util.MaybeGetFlagString(cmd, "ldap-search-attribute")
		ldapSearchFilter := util.MaybeGetFlagString(cmd, "ldap-search-filter")
		if ldapSearchAttribute != "" && ldapSearchFilter != "" {
			logrus.Warn(
				formatter.Colorize(
					"Both --ldap-search-attribute and --ldap-search-filter are specified. --ldap-search-attribute will be ignored.",
					formatter.YellowColor,
				),
			)
		}
		ldapDefaultRole := util.MaybeGetFlagString(cmd, "default-role")
		if ldapDefaultRole != "" && ldapDefaultRole != "ReadOnly" &&
			ldapDefaultRole != "ConnectOnly" {
			logrus.Fatal(
				formatter.Colorize(
					"Invalid value for --default-role. Possible values are ReadOnly and ConnectOnly.",
					formatter.RedColor,
				),
			)
		}
		useGroupSearchQuery := util.MustGetFlagBool(cmd, "group-use-query")
		ldapGroupMemberOfAttrobute := util.GetFlagValueAsStringIfSet(cmd, "group-member-attribute")
		if useGroupSearchQuery && ldapGroupMemberOfAttrobute != "" {
			logrus.Warn(
				formatter.Colorize(
					"Both --group-use-query and --group-member-attribute are specified. --group-member-attribute will be ignored.",
					formatter.YellowColor,
				),
			)
		}
		groupSearchScope := strings.ToUpper(util.MaybeGetFlagString(cmd, "group-search-scope"))
		if groupSearchScope != "" && groupSearchScope != util.LdapGroupSearchScopeObject &&
			groupSearchScope != util.LdapGroupSearchScopeOneLevel &&
			groupSearchScope != util.LdapGroupSearchScopeSubtree {
			logrus.Fatal(
				formatter.Colorize(
					"Invalid value for --group-search-scope. Possible values are OBJECT, ONELEVEL and SUBTREE.",
					formatter.RedColor,
				),
			)
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		configureLDAP(configureLDAPParams{
			Host: util.GetFlagValueAsStringIfSet(cmd, "ldap-host"),
			Port: util.GetFlagValueAsStringIfSet(cmd, "ldap-port"),
			LdapSSLProtocol: strings.ToLower(
				util.GetFlagValueAsStringIfSet(cmd, "ldap-ssl-protocol"),
			),
			LdapTLSVersion:         util.GetFlagValueAsStringIfSet(cmd, "ldap-tls-version"),
			BaseDN:                 util.GetFlagValueAsStringIfSet(cmd, "base-dn"),
			DNPrefix:               util.GetFlagValueAsStringIfSet(cmd, "dn-prefix"),
			CustomerUUID:           util.GetFlagValueAsStringIfSet(cmd, "customer-uuid"),
			SearchAndBind:          util.GetFlagValueAsStringIfSet(cmd, "search-and-bind"),
			LdapSearchAttribute:    util.GetFlagValueAsStringIfSet(cmd, "ldap-search-attribute"),
			LdapSearchFilter:       util.GetFlagValueAsStringIfSet(cmd, "ldap-search-filter"),
			ServiceAccountDN:       util.GetFlagValueAsStringIfSet(cmd, "service-account-dn"),
			ServiceAccountPassword: util.GetFlagValueAsStringIfSet(cmd, "service-account-password"),
			DefaultRole:            util.GetFlagValueAsStringIfSet(cmd, "default-role"),
			GroupAttribute:         util.GetFlagValueAsStringIfSet(cmd, "group-member-attribute"),
			GroupUseQuery:          util.GetFlagValueAsStringIfSet(cmd, "group-use-query"),
			GroupSearchFilter:      util.GetFlagValueAsStringIfSet(cmd, "group-search-filter"),
			GroupSearchBase:        util.GetFlagValueAsStringIfSet(cmd, "group-search-base"),
			GroupSearchScope: strings.ToUpper(
				util.GetFlagValueAsStringIfSet(cmd, "group-search-scope"),
			),
		})
	},
}

func init() {
	configureLDAPCmd.Flags().SortFlags = false
	configureLDAPCmd.Flags().String("ldap-host", "",
		"[Optional] LDAP server host")
	configureLDAPCmd.Flags().Int64("ldap-port", 389,
		"[Optional] LDAP server port")
	configureLDAPCmd.Flags().String("ldap-ssl-protocol", "none",
		"[Optional] LDAP SSL protocol. Allowed values: none, ldaps, starttls.")
	configureLDAPCmd.Flags().String("ldap-tls-version", "TLSv1_2",
		"[Optional] LDAP TLS version. Allowed values (case sensitive): TLSv1, TLSv1_1 and TLSv1_2.")
	configureLDAPCmd.Flags().StringP("base-dn", "b", "",
		"[Optional] Search base DN for LDAP. Must be enclosed in double quotes.")
	configureLDAPCmd.Flags().String("dn-prefix", "CN=",
		"[Optional] Prefix to be appended to the username for LDAP search.\n"+
			"           Must be enclosed in double quotes.")
	configureLDAPCmd.Flags().String("customer-uuid", "",
		"[Optional] YBA Customer UUID for LDAP authentication (Only for multi-tenant YBA)")
	configureLDAPCmd.Flags().Bool("search-and-bind", false,
		"[Optional] Use search and bind for LDAP Authentication. (default false).")
	configureLDAPCmd.Flags().String("ldap-search-attribute", "",
		"[Optional] LDAP search attribute for user authentication.")
	configureLDAPCmd.Flags().String("ldap-search-filter", "",
		"[Optional] LDAP search filter for user authentication.\n"+
			"           Specify this or ldap-search-attribute for LDAP search and bind. Must be enclosed in double quotes")
	configureLDAPCmd.Flags().String("service-account-dn", "",
		"[Optional] Service account DN for LDAP authentication. Must be enclosed in double quotes")
	configureLDAPCmd.Flags().String("service-account-password", "",
		"[Optional] Service account password for LDAP authentication. Must be enclosed in double quotes")
	// Role mapping flags
	configureLDAPCmd.Flags().String("default-role", "ReadOnly",
		"[Optional] Default role for LDAP authentication.\n"+
			"           This role will be used if a role cannot be determined via LDAP.\n"+
			"           Allowed values (case sensitive): ReadOnly, ConnectOnly.")

	configureLDAPCmd.Flags().String("group-member-attribute", "memberOf",
		"[Optional] LDAP attribute that contains user groups.\n"+
			"           Used for mapping LDAP groups to roles.")
	configureLDAPCmd.Flags().Bool("group-use-query", false,
		"[Optional] Enable LDAP query-based role mapping.\n"+
			"           If set, the application will perform an LDAP search to determine a user's groups,\n"+
			"           instead of using the attribute specified by --group-member-attribute. (default false).")

	configureLDAPCmd.MarkFlagsMutuallyExclusive("group-use-query", "group-member-attribute")
	configureLDAPCmd.Flags().String("group-search-filter", "",
		"[Optional] LDAP group search filter for role mapping. Must be enclosed in double quotes")
	configureLDAPCmd.Flags().String("group-search-base", "",
		"[Optional] LDAP group search base for role mapping. Must be enclosed in double quotes")
	configureLDAPCmd.Flags().String("group-search-scope", "SUBTREE",
		"[Optional] LDAP group search scope for role mapping.\n"+
			"           Allowed values: OBJECT, ONELEVEL and SUBTREE.")

}
