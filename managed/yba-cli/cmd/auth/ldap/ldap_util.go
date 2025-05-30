/*
 * Copyright (c) YugaByte, Inc.
 */

package ldap

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/runtimeconfiguration/scope/key"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ldap"
)

type configureLDAPParams struct {
	Host                   string
	Port                   string
	LdapSSLProtocol        string
	LdapTLSVersion         string
	BaseDN                 string
	DNPrefix               string
	CustomerUUID           string
	SearchAndBind          string
	LdapSearchAttribute    string
	LdapSearchFilter       string
	ServiceAccountDN       string
	ServiceAccountPassword string
	DefaultRole            string
	GroupAttribute         string
	GroupUseQuery          string
	GroupSearchFilter      string
	GroupSearchBase        string
	GroupSearchScope       string
}

func disableLDAP(resetAll bool, keysToReset map[string]bool) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	ldapConfig := getScopedConfigWithLDAPKeys(authAPI, false /*inherited*/)
	// Delete the toggle key if present in the config
	if len(ldapConfig) == 0 {
		logrus.Warn(formatter.Colorize("No LDAP configuration found.\n", formatter.YellowColor))
		return
	}
	if resetAll {
		for _, keyConfig := range ldapConfig {
			logrus.Info(
				formatter.Colorize(
					fmt.Sprintf("Deleting key: %s\n", util.LDAPKeyToFlagMap[keyConfig.GetKey()]),
					formatter.GreenColor,
				),
			)
			key.DeleteGlobalKey(authAPI, keyConfig.GetKey())
		}
	} else {
		for _, keyConfig := range ldapConfig {
			if _, exists := keysToReset[keyConfig.GetKey()]; exists {
				logrus.Info(
					formatter.Colorize(
						fmt.Sprintf("Deleting key: %s\n", util.LDAPKeyToFlagMap[keyConfig.GetKey()]),
						formatter.GreenColor,
					),
				)
				key.DeleteGlobalKey(authAPI, keyConfig.GetKey())
			}
		}
	}
	logrus.Info(
		formatter.Colorize("LDAP configuration deleted successfully.\n", formatter.GreenColor))
}

func configureLDAP(params configureLDAPParams) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	key.CheckAndSetGlobalKey(authAPI, util.ToggleLDAPKey, "true")
	key.CheckAndSetGlobalKey(authAPI, util.LDAPHostKey, params.Host)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPPortKey, params.Port)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPTLSVersionKey, params.LdapTLSVersion)
	if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithSSL {
		key.CheckAndSetGlobalKey(authAPI, util.UseLDAPSKey, "true")
		key.CheckAndSetGlobalKey(authAPI, util.UseStartTLSKey, "false")
	} else if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithStartTLS {
		key.CheckAndSetGlobalKey(authAPI, util.UseStartTLSKey, "true")
		key.CheckAndSetGlobalKey(authAPI, util.UseLDAPSKey, "false")
	} else if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithoutSSL {
		key.CheckAndSetGlobalKey(authAPI, util.UseLDAPSKey, "false")
		key.CheckAndSetGlobalKey(authAPI, util.UseStartTLSKey, "false")
	}
	key.CheckAndSetGlobalKey(authAPI, util.LDAPBaseDNKey, params.BaseDN)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPDNPrefixKey, params.DNPrefix)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPCustomerUUIDKey, params.CustomerUUID)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPSearchAndBindKey, params.SearchAndBind)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPSearchAttributeKey, params.LdapSearchAttribute)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPSearchFilterKey, params.LdapSearchFilter)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPServiceAccountDNKey, params.ServiceAccountDN)
	key.CheckAndSetGlobalKey(
		authAPI,
		util.LDAPServiceAccountPasswordKey,
		params.ServiceAccountPassword,
	)

	// Group mapping params
	key.CheckAndSetGlobalKey(authAPI, util.LDAPGroupUseRoleMapping, "true")
	key.CheckAndSetGlobalKey(authAPI, util.LDAPDefaultRoleKey, params.DefaultRole)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPGroupAttributeKey, params.GroupAttribute)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPGroupUseQueryKey, params.GroupUseQuery)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPGroupSearchFilterKey, params.GroupSearchFilter)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPGroupSearchBaseKey, params.GroupSearchBase)
	key.CheckAndSetGlobalKey(authAPI, util.LDAPGroupSearchScopeKey, params.GroupSearchScope)
	logrus.Info(
		formatter.Colorize("LDAP configuration updated successfully.\n", formatter.GreenColor),
	)
	getLDAPConfig(true /*inherited*/)
}

func getLDAPConfig(inherited bool) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	ldapConfig := getScopedConfigWithLDAPKeys(authAPI, inherited)
	if len(ldapConfig) == 0 {
		logrus.Info(formatter.Colorize("No LDAP configuration found.\n", formatter.YellowColor))
		return
	}
	writeLDAPConfig(ldapConfig)
}

func getScopedConfigWithLDAPKeys(
	authAPI *ybaAuthClient.AuthAPIClient,
	inherited bool,
) []ybaclient.ConfigEntry {
	r, response, err := authAPI.GetConfig(util.GlobalScopeUUID).
		IncludeInherited(inherited).
		Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"LDAP config", "Get")
		logrus.Fatal(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	ldapKeys := make([]ybaclient.ConfigEntry, 0, len(r.GetConfigEntries()))
	// Filter out the keys that are not related to LDAP
	for _, keyConfig := range r.GetConfigEntries() {
		if util.IsLDAPKey(keyConfig.GetKey()) {
			ldapKeys = append(ldapKeys, keyConfig)
		}
	}
	return ldapKeys
}

func writeLDAPConfig(ldapConfig []ybaclient.ConfigEntry) {
	logrus.Info(formatter.Colorize("LDAP configuration:\n", formatter.GreenColor))
	ldapConfigCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  ldap.NewLDAPFormat(viper.GetString("output")),
	}
	ldap.Write(ldapConfigCtx, ldapConfig)
}
