/*
 * Copyright (c) YugaByte, Inc.
 */

package ldap

import (
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/runtimeconfiguration/scope/key"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/scope"
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

func disableLDAP() {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	deleteKey(authAPI, toggleLDAPKey)
}

func configureLDAP(params configureLDAPParams) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	checkAndSetKey(authAPI, toggleLDAPKey, "true")
	checkAndSetKey(authAPI, ldapHostKey, params.Host)
	checkAndSetKey(authAPI, ldapPortKey, params.Port)
	checkAndSetKey(authAPI, ldapTLSVersionKey, params.LdapTLSVersion)
	if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithSSL {
		checkAndSetKey(authAPI, useLDAPSKey, "true")
		checkAndSetKey(authAPI, useStartTLSKey, "false")
	} else if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithStartTLS {
		checkAndSetKey(authAPI, useStartTLSKey, "true")
		checkAndSetKey(authAPI, useLDAPSKey, "false")
	} else if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithoutSSL {
		checkAndSetKey(authAPI, useLDAPSKey, "false")
		checkAndSetKey(authAPI, useStartTLSKey, "false")
	}
	checkAndSetKey(authAPI, ldapBaseDNKey, params.BaseDN)
	checkAndSetKey(authAPI, ldapDNPrefixKey, params.DNPrefix)
	checkAndSetKey(authAPI, ldapCustomerUUIDKey, params.CustomerUUID)
	checkAndSetKey(authAPI, ldapSearchAndBindKey, params.SearchAndBind)
	checkAndSetKey(authAPI, ldapSearchAttributeKey, params.LdapSearchAttribute)
	checkAndSetKey(authAPI, ldapSearchFilterKey, params.LdapSearchFilter)
	checkAndSetKey(authAPI, ldapServiceAccountDNKey, params.ServiceAccountDN)
	checkAndSetKey(authAPI, ldapServiceAccountPasswordKey, params.ServiceAccountPassword)

	// Group mapping params
	checkAndSetKey(authAPI, ldapGroupUseRoleMapping, "true")
	checkAndSetKey(authAPI, ldapDefaultRoleKey, params.DefaultRole)
	checkAndSetKey(authAPI, ldapGroupAttributeKey, params.GroupAttribute)
	checkAndSetKey(authAPI, ldapGroupUseQueryKey, params.GroupUseQuery)
	checkAndSetKey(authAPI, ldapGroupSearchFilterKey, params.GroupSearchFilter)
	checkAndSetKey(authAPI, ldapGroupSearchBaseKey, params.GroupSearchBase)
	checkAndSetKey(authAPI, ldapGroupSearchScopeKey, params.GroupSearchScope)
	logrus.Info(
		formatter.Colorize("LDAP has been configured successfully.\n", formatter.GreenColor),
	)
	getLDAPConfig(authAPI)
}

func getLDAPConfig(authAPI *ybaAuthClient.AuthAPIClient) {
	r, response, err := authAPI.GetConfig(util.GlobalScopeUUID).IncludeInherited(true).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"LDAP config", "Get")
		logrus.Fatal(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	ldapKeys := []ybaclient.ConfigEntry{}
	// Filter out the keys that are not related to LDAP
	for _, keyConfig := range r.GetConfigEntries() {
		if strings.HasPrefix(keyConfig.GetKey(), "yb.security.ldap") {
			ldapKeys = append(ldapKeys, keyConfig)
		}
	}
	r.ConfigEntries = &ldapKeys
	fullScopeContext := *scope.NewFullScopeContext()
	fullScopeContext.Output = os.Stdout
	fullScopeContext.Format = scope.NewFullScopeFormat(viper.GetString("output"))
	fullScopeContext.SetFullScope(r)
	fullScopeContext.Write()
}

func checkAndSetKey(authAPI *ybaAuthClient.AuthAPIClient, keyName, value string) {
	if value != "" {
		key.SetKey(authAPI, util.GlobalScopeUUID, keyName, value, false /*logSuccess*/)
	}
}

func deleteKey(authAPI *ybaAuthClient.AuthAPIClient, keyName string) {
	key.DeleteKey(authAPI, util.GlobalScopeUUID, keyName)
}
