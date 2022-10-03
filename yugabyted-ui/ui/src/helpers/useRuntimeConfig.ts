import { useGetAppConfigQuery, GetAppConfigScopeTypeEnum, AppConfigResponse } from '@app/api/src';
import type { UseQueryResult } from 'react-query';

// TODO: handle all UI feature flags here
interface RuntimeConfig {
  PLGOnboardingPhase1: boolean;
  MultiRegionEnabled: boolean;
  SSOGoogleConfigEnabled: boolean;
  SignupCompanyFieldRequired: boolean;
}

const PLG_FEATURE_FLAG = 'ybcloud.conf.plg_user_onboarding.phase_1.enabled';
const MULTI_REGION_FLAG_PATH = 'ybcloud.conf.cluster.geo_partitioning.enabled';
const SSO_GOOGLE_CONFIG_PATH = 'ybcloud.conf.security.idp.google.enabled';
const SIGNUP_COMPANY_REQUIRED_PATH = 'ybcloud.conf.ui.signup_company_field_required';

const globalPaths = [PLG_FEATURE_FLAG, MULTI_REGION_FLAG_PATH, SSO_GOOGLE_CONFIG_PATH, SIGNUP_COMPANY_REQUIRED_PATH];

const accountPaths = [PLG_FEATURE_FLAG, MULTI_REGION_FLAG_PATH];

const transformResponse = (response: AppConfigResponse): RuntimeConfig => {
  const runtimeConfig = {} as RuntimeConfig;

  // manually process each item to convert string value into a proper type known to flag author only
  response.data.forEach((item) => {
    if (item.path === PLG_FEATURE_FLAG) {
      runtimeConfig.PLGOnboardingPhase1 = item.value === 'true';
    }
    if (item.path === MULTI_REGION_FLAG_PATH) {
      runtimeConfig.MultiRegionEnabled = item.value === 'true';
    }
    if (item.path === SSO_GOOGLE_CONFIG_PATH) {
      runtimeConfig.SSOGoogleConfigEnabled = item.value === 'true';
    }
    if (item.path === SIGNUP_COMPANY_REQUIRED_PATH) {
      runtimeConfig.SignupCompanyFieldRequired = item.value === 'true';
    }
  });

  return runtimeConfig;
};

export const useRuntimeConfig = (accountId?: string): UseQueryResult<RuntimeConfig> => {
  return useGetAppConfigQuery(
    {
      paths: accountId ? accountPaths : globalPaths,
      scopeType: accountId ? GetAppConfigScopeTypeEnum.Account : GetAppConfigScopeTypeEnum.Global,
      scopeId: accountId ?? undefined
    },
    {
      query: { select: transformResponse }
    }
  );
};
