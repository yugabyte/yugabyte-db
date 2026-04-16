/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

// LDAP keys for YBA
const (
	ToggleLDAPKey                 = "yb.security.ldap.use_ldap"
	LDAPHostKey                   = "yb.security.ldap.ldap_url"
	LDAPPortKey                   = "yb.security.ldap.ldap_port"
	UseLDAPSKey                   = "yb.security.ldap.enable_ldaps"
	UseStartTLSKey                = "yb.security.ldap.enable_ldap_start_tls"
	LDAPTLSVersionKey             = "yb.security.ldap.ldap_tls_protocol"
	LDAPBaseDNKey                 = "yb.security.ldap.ldap_basedn"
	LDAPDNPrefixKey               = "yb.security.ldap.ldap_dn_prefix"
	LDAPCustomerUUIDKey           = "yb.security.ldap.ldap_customeruuid"
	LDAPSearchAndBindKey          = "yb.security.ldap.use_search_and_bind"
	LDAPSearchAttributeKey        = "yb.security.ldap.ldap_search_attribute"
	LDAPSearchFilterKey           = "yb.security.ldap.ldap_search_filter"
	LDAPServiceAccountDNKey       = "yb.security.ldap.ldap_service_account_distinguished_name"
	LDAPServiceAccountPasswordKey = "yb.security.ldap.ldap_service_account_password"
	LDAPDefaultRoleKey            = "yb.security.ldap.ldap_default_role"
	LDAPGroupAttributeKey         = "yb.security.ldap.ldap_group_member_of_attribute"
	LDAPGroupSearchFilterKey      = "yb.security.ldap.ldap_group_search_filter"
	LDAPGroupSearchBaseKey        = "yb.security.ldap.ldap_group_search_base_dn"
	LDAPGroupSearchScopeKey       = "yb.security.ldap.ldap_group_search_scope"
	LDAPGroupUseQueryKey          = "yb.security.ldap.ldap_group_use_query"
	LDAPGroupUseRoleMapping       = "yb.security.ldap.ldap_group_use_role_mapping"
)

// LDAPKeyToFlagMap is a map of LDAP keys to their corresponding flags
var LDAPKeyToFlagMap = map[string]string{
	ToggleLDAPKey:                 "ldap-enabled",
	LDAPHostKey:                   "ldap-host",
	LDAPPortKey:                   "ldap-port",
	UseLDAPSKey:                   "ldaps-enabled",
	UseStartTLSKey:                "start-tls-enabled",
	LDAPTLSVersionKey:             "tls-version",
	LDAPBaseDNKey:                 "base-dn",
	LDAPDNPrefixKey:               "dn-prefix",
	LDAPCustomerUUIDKey:           "customer-uuid",
	LDAPSearchAndBindKey:          "search-and-bind-enabled",
	LDAPSearchAttributeKey:        "search-attribute",
	LDAPSearchFilterKey:           "search-filter",
	LDAPServiceAccountDNKey:       "service-account-dn",
	LDAPServiceAccountPasswordKey: "service-account-password",
	LDAPDefaultRoleKey:            "default-role",
	LDAPGroupAttributeKey:         "group-attribute",
	LDAPGroupSearchFilterKey:      "group-search-filter",
	LDAPGroupSearchBaseKey:        "group-search-base",
	LDAPGroupSearchScopeKey:       "group-search-scope",
	LDAPGroupUseQueryKey:          "group-use-query",
	LDAPGroupUseRoleMapping:       "group-use-role-mapping",
}

// IsLDAPKey checks if the given key is an LDAP key
func IsLDAPKey(key string) bool {
	_, exists := LDAPKeyToFlagMap[key]
	return exists
}

// ResetFlagToLDAPKeyMap is a map of LDAP keys to their corresponding reset flags
var ResetFlagToLDAPKeyMap = map[string]string{
	"ldap-host":                LDAPHostKey,
	"ldap-port":                LDAPPortKey,
	"ldaps-enabled":            UseLDAPSKey,
	"start-tls-enabled":        UseStartTLSKey,
	"tls-version":              LDAPTLSVersionKey,
	"base-dn":                  LDAPBaseDNKey,
	"dn-prefix":                LDAPDNPrefixKey,
	"customer-uuid":            LDAPCustomerUUIDKey,
	"search-and-bind-enabled":  LDAPSearchAndBindKey,
	"search-attribute":         LDAPSearchAttributeKey,
	"search-filter":            LDAPSearchFilterKey,
	"service-account-dn":       LDAPServiceAccountDNKey,
	"service-account-password": LDAPServiceAccountPasswordKey,
	"default-role":             LDAPDefaultRoleKey,
	"group-attribute":          LDAPGroupAttributeKey,
	"group-search-filter":      LDAPGroupSearchFilterKey,
	"group-search-base":        LDAPGroupSearchBaseKey,
	"group-search-scope":       LDAPGroupSearchScopeKey,
	"group-use-query":          LDAPGroupUseQueryKey,
	"group-use-role-mapping":   LDAPGroupUseRoleMapping,
}

// GetLDAPKeyForResetFlag returns the LDAP key for the given reset flag
func GetLDAPKeyForResetFlag(flag string) (string, bool) {
	key, exists := ResetFlagToLDAPKeyMap[flag]
	return key, exists
}
