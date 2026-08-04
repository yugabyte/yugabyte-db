/*
 * Created on Mon Aug 05 2024
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

export interface OIDCFormProps {
  use_oauth: boolean;
  type: string;
  clientID: string;
  secret: string;
  discoveryURI: string;
  oidcProviderMetadata: string;
  oidcScope: string;
  oidcEmailAttribute: string;
  showJWTInfoOnLogin: string;
  oidcRefreshTokenEndpoint: string;
  oidc_default_role: string;
  oidc_group_claim: string;
  oidc_enable_auto_create_users: boolean;
}

export const OIDC_FIELDS: Array<keyof OIDCFormProps> = [
  'use_oauth',
  'type',
  'clientID',
  'secret',
  'discoveryURI',
  'oidcProviderMetadata',
  'oidcScope',
  'oidcEmailAttribute',
  'showJWTInfoOnLogin',
  'oidcRefreshTokenEndpoint',
  'oidc_default_role',
  'oidc_group_claim',
  'oidc_enable_auto_create_users'
];
