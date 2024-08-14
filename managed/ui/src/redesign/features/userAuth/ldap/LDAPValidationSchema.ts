/*
 * Created on Mon Aug 05 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { LDAPFormProps } from './LDAPConstants';

export const getLDAPValidationSchema = (t: TFunction) => {
  return Yup.object<Partial<LDAPFormProps>>({
    ldap_url: Yup.string()
      .matches(/^(?:(http|https|ldap|ldaps)?:\/\/)?[\w.-]+(?:[\w-]+)+:\d{1,5}$/, {
        message: t('messages.ldapURLPortNo')
      })
      .required(t('messages.ldapURL')),
    ldap_security: Yup.string().required(t('messages.ldapSecurity')),
    ldap_service_account_password: Yup.string().when('ldap_service_account_distinguished_name', {
      is: (username) => username && username.length > 0,
      then: Yup.string().required(t('messages.password')),
      otherwise: Yup.string()
    }),
    ldap_search_attribute: Yup.string().when(['use_search_and_bind', 'ldap_search_filter'], {
      is: (use_search_and_bind, ldap_search_filter) =>
        use_search_and_bind === 'true' && (!ldap_search_filter || ldap_search_filter.trim() === ''),
      then: Yup.string().required(t('messages.searchAttributeOrFilter')),
      otherwise: Yup.string()
    }),
    ldap_group_member_of_attribute: Yup.string().when(
      ['ldap_group_use_query', 'ldap_group_use_role_mapping'],
      {
        is: (ldap_group_use_query, ldap_group_use_role_mapping) =>
          ldap_group_use_role_mapping === true && ldap_group_use_query === 'false',
        then: Yup.string().required(t('messages.userAttribute')),
        otherwise: Yup.string()
      }
    ),
    ldap_group_search_filter: Yup.string().when(
      ['ldap_group_use_query', 'ldap_group_use_role_mapping'],
      {
        is: (ldap_group_use_query, ldap_group_use_role_mapping) =>
          ldap_group_use_role_mapping === true && ldap_group_use_query === 'true',
        then: Yup.string().required(t('messages.searchFilter')),
        otherwise: Yup.string()
      }
    ),
    ldap_group_search_base_dn: Yup.string().when(
      ['ldap_group_use_query', 'ldap_group_use_role_mapping'],
      {
        is: (ldap_group_use_query, ldap_group_use_role_mapping) =>
          ldap_group_use_role_mapping === true && ldap_group_use_query === 'true',
        then: Yup.string().required(t('messages.groupSearchBN')),
        otherwise: Yup.string()
      }
    ),
    ldap_service_account_distinguished_name: Yup.string().when(
      ['use_search_and_bind', 'ldap_group_use_role_mapping'],
      {
        is: (use_search_and_bind, ldap_group_use_role_mapping) =>
          use_search_and_bind === 'true' || ldap_group_use_role_mapping === true,
        then: Yup.string().required(t('messages.bindDN')),
        otherwise: Yup.string()
      }
    ),
    ldap_tls_protocol: Yup.string().when(['ldap_security'], {
      is: (ldap_security) => ['enable_ldaps', 'enable_ldap_start_tls'].includes(ldap_security),
      then: Yup.string().required(t('messages.tlsVersion')),
      otherwise: Yup.string()
    }),
    ldap_basedn: Yup.string().required(t('messages.baseDN'))
  });
};
