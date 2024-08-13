/*
 * Created on Thu Aug 01 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

export const LDAPPath = 'yb.security.ldap';
export const LDAP_RUNTIME_CONFIGS_QUERY_KEY = 'LDAP_RUNTIME_CONFIGS_QUERY_KEY';

export interface LDAPFormProps {
  use_ldap: boolean;
  ldap_url: string;
  ldap_security: string;
  ldap_tls_protocol: string;
  ldap_basedn: string;
  ldap_dn_prefix: string;
  ldap_customeruuid: string;
  use_search_and_bind: boolean;
  ldap_search_attribute: string;
  ldap_search_filter: string;
  ldap_default_role: string;
  ldap_group_member_of_attribute: string;
  ldap_group_search_base_dn: string;
  ldap_group_search_scope: typeof LDAPScopes[number];
  use_service_account: boolean;
  ldap_service_account_distinguished_name: string;
  ldap_service_account_password: string;
  ldap_group_use_query: boolean;
  ldap_group_search_filter: string;
}

export const TLSVersions = [
  {
    label: 'TLSv1',
    value: 'TLSv1'
  },
  {
    label: 'TLSv1.1',
    value: 'TLSv1_1'
  },
  {
    label: 'TLSv1.2',
    value: 'TLSv1_2'
  }
];

export enum SecurityOption {
  ENABLE_LDAPS = 'enable_ldaps',
  ENABLE_LDAP_START_TLS = 'enable_ldap_start_tls',
  UNSECURE = 'unsecure'
}

export const LDAPSecurityOptions = [
  {
    label: 'Secure LDAP (LDAPS)',
    value: SecurityOption.ENABLE_LDAPS
  },
  {
    label: 'LDAP with StartTLS',
    value: SecurityOption.ENABLE_LDAP_START_TLS
  },
  {
    label: 'None',
    value: SecurityOption.UNSECURE
  }
];

export const AuthModes = [
  {
    label: 'Simple Bind',
    value: 'false'
  },
  {
    label: 'Search and Bind',
    value: 'true'
  }
];

export enum LDAPUseQuery {
  USER_ATTRIBUTE = 'true',
  GROUP_SEARCH_FILTER = 'false'
}

export const LDAPUseQueryOptions = [
  {
    label: 'User Attribute',
    value: 'true'
  },
  {
    label: 'Group Search Filter',
    value: 'false'
  }
];

export const LDAPScopes = [
  {
    label: 'Object Scope',
    value: 'OBJECT'
  },
  {
    label: 'One Level Scope',
    value: 'ONELEVEL'
  },
  {
    label: 'Subtree Scope',
    value: 'SUBTREE'
  }
];
