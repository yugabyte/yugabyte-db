/*
 * Created on Tue Jul 23 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext } from 'react';
import { GroupContextMethods, GroupViewContext } from './GroupContext';
import { RunTimeConfig } from '../../../universe/universe-form/utils/dto';
import { find } from 'lodash';
import { YBTooltip } from '../../../../components';

export function GetGroupContext() {
  return (useContext(GroupViewContext) as unknown) as GroupContextMethods;
}

export const OIDC_RUNTIME_CONFIGS_QUERY_KEY = 'OIDC_RUNTIME_CONFIGS';

export const OIDC_PATH = 'yb.security';
export const LDAP_PATH = 'yb.security.ldap';

const OIDC_CONFIG_KEY = `${OIDC_PATH}.use_oauth`;
const LDAP_CONFIG_KEY = `${LDAP_PATH}.use_ldap`;

const isEnabled = (runtimeConfigs: RunTimeConfig, configKey: string) => {
  return find(runtimeConfigs.configEntries, (config) => config.key === configKey)?.value === 'true';
};

export const getIsOIDCEnabled = (runtimeConfigs: RunTimeConfig) => {
  return isEnabled(runtimeConfigs, OIDC_CONFIG_KEY);
};

export const getIsLDAPEnabled = (runtimeConfigs: RunTimeConfig) => {
  return isEnabled(runtimeConfigs, LDAP_CONFIG_KEY);
};

export const WrapDisabledElements = (children: JSX.Element, disabled: boolean, title: string) => {
  if (!disabled) return children;
  return <YBTooltip title={title}>{children}</YBTooltip>;
};
