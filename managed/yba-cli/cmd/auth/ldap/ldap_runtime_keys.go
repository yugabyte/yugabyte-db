/*
 * Copyright (c) YugaByte, Inc.
 */

package ldap

const (
	toggleLDAPKey                 = "yb.security.ldap.use_ldap"
	ldapHostKey                   = "yb.security.ldap.ldap_url"
	ldapPortKey                   = "yb.security.ldap.ldap_port"
	useLDAPSKey                   = "yb.security.ldap.enable_ldaps"
	useStartTLSKey                = "yb.security.ldap.enable_ldap_start_tls"
	ldapTLSVersionKey             = "yb.security.ldap.ldap_tls_protocol"
	ldapBaseDNKey                 = "yb.security.ldap.ldap_basedn"
	ldapDNPrefixKey               = "yb.security.ldap.ldap_dn_prefix"
	ldapCustomerUUIDKey           = "yb.security.ldap.ldap_customer_uuid"
	ldapSearchAndBindKey          = "yb.security.ldap.use_search_and_bind"
	ldapSearchAttributeKey        = "yb.security.ldap.ldap_search_attribute"
	ldapSearchFilterKey           = "yb.security.ldap.ldap_search_filter"
	ldapServiceAccountDNKey       = "yb.security.ldap.ldap_service_account_distinguished_name"
	ldapServiceAccountPasswordKey = "yb.security.ldap.ldap_service_account_password"
	ldapDefaultRoleKey            = "yb.security.ldap.ldap_default_role"
	ldapGroupAttributeKey         = "yb.security.ldap.ldap_group_member_of_attribute"
	ldapGroupSearchFilterKey      = "yb.security.ldap.ldap_group_search_filter"
	ldapGroupSearchBaseKey        = "yb.security.ldap.ldap_group_search_base_dn"
	ldapGroupSearchScopeKey       = "yb.security.ldap.ldap_group_search_scope"
	ldapGroupUseQueryKey          = "yb.security.ldap.ldap_group_use_query"
	ldapGroupUseRoleMapping       = "yb.security.ldap.ldap_group_use_role_mapping"
)
