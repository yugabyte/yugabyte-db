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
	key.DeleteGlobalKey(authAPI, toggleLDAPKey)
}

func configureLDAP(params configureLDAPParams) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	key.CheckAndSetGlobalKey(authAPI, toggleLDAPKey, "true")
	key.CheckAndSetGlobalKey(authAPI, ldapHostKey, params.Host)
	key.CheckAndSetGlobalKey(authAPI, ldapPortKey, params.Port)
	key.CheckAndSetGlobalKey(authAPI, ldapTLSVersionKey, params.LdapTLSVersion)
	if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithSSL {
		key.CheckAndSetGlobalKey(authAPI, useLDAPSKey, "true")
		key.CheckAndSetGlobalKey(authAPI, useStartTLSKey, "false")
	} else if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithStartTLS {
		key.CheckAndSetGlobalKey(authAPI, useStartTLSKey, "true")
		key.CheckAndSetGlobalKey(authAPI, useLDAPSKey, "false")
	} else if strings.ToLower(params.LdapSSLProtocol) == util.LDAPWithoutSSL {
		key.CheckAndSetGlobalKey(authAPI, useLDAPSKey, "false")
		key.CheckAndSetGlobalKey(authAPI, useStartTLSKey, "false")
	}
	key.CheckAndSetGlobalKey(authAPI, ldapBaseDNKey, params.BaseDN)
	key.CheckAndSetGlobalKey(authAPI, ldapDNPrefixKey, params.DNPrefix)
	key.CheckAndSetGlobalKey(authAPI, ldapCustomerUUIDKey, params.CustomerUUID)
	key.CheckAndSetGlobalKey(authAPI, ldapSearchAndBindKey, params.SearchAndBind)
	key.CheckAndSetGlobalKey(authAPI, ldapSearchAttributeKey, params.LdapSearchAttribute)
	key.CheckAndSetGlobalKey(authAPI, ldapSearchFilterKey, params.LdapSearchFilter)
	key.CheckAndSetGlobalKey(authAPI, ldapServiceAccountDNKey, params.ServiceAccountDN)
	key.CheckAndSetGlobalKey(authAPI, ldapServiceAccountPasswordKey, params.ServiceAccountPassword)

	// Group mapping params
	key.CheckAndSetGlobalKey(authAPI, ldapGroupUseRoleMapping, "true")
	key.CheckAndSetGlobalKey(authAPI, ldapDefaultRoleKey, params.DefaultRole)
	key.CheckAndSetGlobalKey(authAPI, ldapGroupAttributeKey, params.GroupAttribute)
	key.CheckAndSetGlobalKey(authAPI, ldapGroupUseQueryKey, params.GroupUseQuery)
	key.CheckAndSetGlobalKey(authAPI, ldapGroupSearchFilterKey, params.GroupSearchFilter)
	key.CheckAndSetGlobalKey(authAPI, ldapGroupSearchBaseKey, params.GroupSearchBase)
	key.CheckAndSetGlobalKey(authAPI, ldapGroupSearchScopeKey, params.GroupSearchScope)
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
