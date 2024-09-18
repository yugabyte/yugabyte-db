/*
 * Created on Fri Aug 02 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { omit } from 'lodash';
import { LDAPFormProps } from './LDAPConstants';

export const transformData = (values: LDAPFormProps): Record<string, any> => {
  const splittedURL = values.ldap_url.split(':');
  const ldap_port = splittedURL[splittedURL.length - 1];
  const ldap_url = values.ldap_url.replace(`:${ldap_port}`, '');
  const ldap_basedn = values.ldap_basedn;
  const security = values.ldap_security;
  const use_search_and_bind = values.use_search_and_bind;

  const transformedData = {
    ...values,
    ldap_url: ldap_url ?? '',
    ldap_port: ldap_port ?? '',
    ldap_basedn: ldap_basedn ?? '',
    enable_ldaps: `${security === 'enable_ldaps'}`,
    enable_ldap_start_tls: `${security === 'enable_ldap_start_tls'}`
  };

  if (String(use_search_and_bind) === 'false') {
    transformedData.ldap_search_attribute = '';
    transformedData.ldap_search_filter = '';
  }

  if (values.ldap_group_search_scope) {
    transformedData.ldap_group_search_scope = values.ldap_group_search_scope;
  }
  if (!values.ldap_service_account_distinguished_name) {
    transformedData.ldap_service_account_distinguished_name = '';
    transformedData.ldap_service_account_password = '';
  }

  return omit(transformedData, 'ldap_security', 'use_service_account');
};
