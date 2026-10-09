import { sortBy } from 'lodash';
import {
  AWS_CLOUD_OPTION,
  AZURE_CLOUD_OPTION,
  GCP_CLOUD_OPTION,
  K8S_CLOUD_OPTION,
  OCI_CLOUD_OPTION,
  ON_PREM_CLOUD_OPTION
} from '@yugabyte-ui-library/core';
import {
  ProviderCode,
  ProviderStatus
} from '@app/components/configRedesign/providerRedesign/constants';
import { YBProvider } from '@app/components/configRedesign/providerRedesign/types';
import {
  api,
  DBReleasesQueryKey,
  QUERY_KEY
} from '@app/redesign/features/universe/universe-form/utils/api';
import {
  CloudType,
  YBSoftwareMetadata
} from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  getActiveDBVersions,
  sortVersionStrings
} from '@app/redesign/features/universe/universe-form/form/fields/DBVersionField/DBVersionHelper';
import { ReleaseState } from '@app/redesign/features/releases/components/dtos';
import { isVersionStable } from '@app/utils/universeUtilsTyped';
import { generateUniqueName } from '@app/redesign/helpers/utils';
import { GeneralSettingsProps, ProviderType } from '../steps/general-settings/dtos';

export const BASE_CLOUD_OPTIONS = [
  { ...AWS_CLOUD_OPTION, value: CloudType.aws },
  { ...GCP_CLOUD_OPTION, value: CloudType.gcp },
  { ...AZURE_CLOUD_OPTION, value: CloudType.azu },
  { ...K8S_CLOUD_OPTION, value: CloudType.kubernetes },
  { ...ON_PREM_CLOUD_OPTION, value: CloudType.onprem },
  { ...OCI_CLOUD_OPTION, value: CloudType.oci }
];

export const CREATE_UNIVERSE_PROVIDERS_QUERY_KEY = QUERY_KEY.getProvidersList;
export const CREATE_UNIVERSE_DB_VERSIONS_QUERY_KEY = DBReleasesQueryKey.ALL;

export const fetchCreateUniverseProviders = () =>
  api.getProvidersList() as Promise<YBProvider[]>;
export const fetchCreateUniverseDbVersions = () => api.getDBVersions(true, null, true);

export type ProviderListItem = Pick<YBProvider, 'uuid' | 'code' | 'name' | 'usabilityState'> & {
  details?: { skipProvisioning?: boolean };
};

export const getReadyProviders = (
  providers: ProviderListItem[] | undefined,
  cloud?: string | null
): ProviderListItem[] => {
  const ready = (providers ?? []).filter(
    (provider) =>
      provider.usabilityState === ProviderStatus.READY &&
      (!cloud || provider.code === cloud)
  );
  return sortBy(ready, 'code', 'name');
};

export const getOrderedCloudOptions = (providers: ProviderListItem[] | undefined) => {
  const configuredCodes = new Set(getReadyProviders(providers).map((provider) => provider.code));
  const configured = BASE_CLOUD_OPTIONS.filter((option) =>
    configuredCodes.has(option.value)
  ).sort((a, b) => a.label.localeCompare(b.label));
  const unconfigured = BASE_CLOUD_OPTIONS.filter((option) => !configuredCodes.has(option.value));
  return {
    configuredCodes,
    clouds: [...configured, ...unconfigured]
  };
};

export const getDefaultCloudType = (providers: ProviderListItem[] | undefined): CloudType => {
  const { configuredCodes, clouds } = getOrderedCloudOptions(providers);
  return configuredCodes.size > 0 ? (clouds[0].value as CloudType) : CloudType.aws;
};

export const toProviderFormValue = (provider: ProviderListItem): ProviderType => {
  const isOnPremManuallyProvisioned =
    provider.code === ProviderCode.ON_PREM && !!provider.details?.skipProvisioning;
  return {
    ...provider,
    isOnPremManuallyProvisioned
  } as ProviderType;
};

export const getDefaultProviderForCloud = (
  providers: ProviderListItem[] | undefined,
  cloud?: string | null
): ProviderType | null => {
  const first = getReadyProviders(providers, cloud)[0];
  return first ? toProviderFormValue(first) : null;
};

export const isProviderValidForCloud = (
  provider: { uuid?: string } | null | undefined,
  cloud: string | null | undefined,
  providers: ProviderListItem[] | undefined
): boolean => {
  if (!provider?.uuid || !cloud) return false;
  return getReadyProviders(providers, cloud).some((item) => item.uuid === provider.uuid);
};

export const resolveProviderOnCloudSync = ({
  previousCloud,
  currentCloud,
  currentProvider,
  providers
}: {
  previousCloud?: string;
  currentCloud: string;
  currentProvider?: { uuid?: string } | null;
  providers?: ProviderListItem[];
}): { shouldApply: boolean; provider: ProviderType | null } => {
  const cloudChanged = previousCloud !== undefined && previousCloud !== currentCloud;
  const isFirstSync = previousCloud === undefined;
  const currentValid = isProviderValidForCloud(currentProvider, currentCloud, providers);

  if (cloudChanged || (isFirstSync && !currentValid)) {
    return {
      shouldApply: true,
      provider: getDefaultProviderForCloud(providers, currentCloud)
    };
  }

  return { shouldApply: false, provider: null };
};

export const transformDbVersionQueryData = (
  data: string[] | Record<string, YBSoftwareMetadata>
) => {
  if (data && Array.isArray(data)) {
    return data.map((item) => ({
      label: item,
      value: item
    }));
  }
  if (typeof data === 'object' && data !== null) {
    return getActiveDBVersions(data);
  }
  return [];
};

export const getLatestStableDbVersion = (
  data: Record<string, any>[] | undefined
): string | undefined => {
  if (!data?.length) return undefined;
  const stableSorted: Record<string, string>[] = sortVersionStrings(
    data.filter((versionData: any) => {
      const label = versionData?.label;
      if (typeof label === 'string') {
        return isVersionStable(label);
      }
      return label?.state === ReleaseState.ACTIVE && isVersionStable(label.version);
    }),
    true
  );
  return stableSorted[0]?.value;
};

export const buildGeneralSettingsDefaults = (
  providers: ProviderListItem[] | undefined,
  dbVersions: Record<string, any>[] | undefined,
  existing?: GeneralSettingsProps
): GeneralSettingsProps => {
  const cloud = existing?.cloud || getDefaultCloudType(providers);
  const provider = isProviderValidForCloud(existing?.providerConfiguration, cloud, providers)
    ? existing?.providerConfiguration
    : getDefaultProviderForCloud(providers, cloud) ?? undefined;

  return {
    universeName: existing?.universeName || generateUniqueName(),
    cloud,
    providerConfiguration: provider,
    databaseVersion: existing?.databaseVersion || getLatestStableDbVersion(dbVersions)
  };
};
