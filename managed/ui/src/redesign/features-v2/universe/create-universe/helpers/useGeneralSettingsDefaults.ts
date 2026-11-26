import { useEffect, useRef } from 'react';
import { UseFormReturn } from 'react-hook-form';
import { useQuery } from 'react-query';
import { generateUniqueName } from '@app/redesign/helpers/utils';
import { GeneralSettingsProps } from '../steps/general-settings/dtos';
import {
  CLOUD,
  DATABASE_VERSION,
  PROVIDER_CONFIGURATIONS,
  UNIVERSE_NAME
} from '../fields/FieldNames';
import {
  CREATE_UNIVERSE_DB_VERSIONS_QUERY_KEY,
  CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
  fetchCreateUniverseDbVersions,
  fetchCreateUniverseProviders,
  collapseProvidersToSingleCloud,
  getDefaultCloudType,
  getLatestStableDbVersion,
  resolveProviderOnCloudSync,
  syncProvidersForClouds,
  transformDbVersionQueryData
} from './generalSettingsDefaults';
import { cloudCodes, normalizeProviderConfigurations } from './providerConfigurations';

type GeneralSettingsForm = Pick<
  UseFormReturn<GeneralSettingsProps>,
  'setValue' | 'getValues' | 'watch'
>;

const cloudsKey = (cloud: GeneralSettingsProps['cloud'] | undefined) => cloudCodes(cloud).join(',');

/**
 * Fills General Settings defaults from the shared provider + DB version queries.
 * Re-selects the first ready provider whenever CloudType changes, including
 * switching from a CloudType with an empty provider list to one that has providers.
 * When `cloud` is a string[], syncs one config per selected cloud (selection order).
 */
export const useGeneralSettingsDefaults = ({
  setValue,
  getValues,
  watch
}: GeneralSettingsForm): void => {
  const cloud = watch(CLOUD);
  const previousCloudKeyRef = useRef<string | undefined>();

  const { data: providers, isSuccess: providersReady } = useQuery(
    CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
    fetchCreateUniverseProviders
  );

  const { data: dbVersions } = useQuery(
    CREATE_UNIVERSE_DB_VERSIONS_QUERY_KEY,
    fetchCreateUniverseDbVersions,
    { select: transformDbVersionQueryData }
  );

  useEffect(() => {
    if (!getValues(UNIVERSE_NAME)) {
      setValue(UNIVERSE_NAME, generateUniqueName());
    }
  }, [getValues, setValue]);

  useEffect(() => {
    if (!providersReady) return;

    let currentCloud = getValues(CLOUD);
    if (!cloudCodes(currentCloud).length) {
      currentCloud = getDefaultCloudType(providers);
      setValue(CLOUD, currentCloud);
    }

    const codes = cloudCodes(currentCloud);
    const currentKey = cloudsKey(currentCloud);
    const previousKey = previousCloudKeyRef.current;
    previousCloudKeyRef.current = currentKey;

    if (codes.length > 1 || Array.isArray(currentCloud)) {
      const next = syncProvidersForClouds(codes, getValues(PROVIDER_CONFIGURATIONS), providers);
      const prev = getValues(PROVIDER_CONFIGURATIONS) ?? [];
      const same =
        prev.length === next.length && prev.every((p, i) => p?.uuid === next[i]?.uuid);
      if (!same) {
        setValue(PROVIDER_CONFIGURATIONS, next, { shouldValidate: true });
      }
      return;
    }

    const singleCloud = codes[0];
    const currentProviders = getValues(PROVIDER_CONFIGURATIONS) ?? [];
    if (currentProviders.length > 1) {
      setValue(
        PROVIDER_CONFIGURATIONS,
        collapseProvidersToSingleCloud(singleCloud, currentProviders, providers),
        { shouldValidate: true }
      );
      return;
    }

    const currentProvider = currentProviders[0];
    const previousCodes = previousKey?.split(',').filter(Boolean) ?? [];
    // Multi → single (Span off) must not look like a cloud change or we replace the primary.
    const previousCloudForSync =
      previousKey === undefined
        ? undefined
        : previousCodes.length === 1
          ? previousCodes[0]
          : singleCloud;
    const { shouldApply, provider } = resolveProviderOnCloudSync({
      previousCloud: previousCloudForSync,
      currentCloud: singleCloud,
      currentProvider,
      providers
    });

    if (shouldApply) {
      setValue(
        PROVIDER_CONFIGURATIONS,
        normalizeProviderConfigurations(provider ? [provider] : []),
        { shouldValidate: true }
      );
    }
  }, [cloud, providers, providersReady, getValues, setValue]);

  useEffect(() => {
    if (getValues(DATABASE_VERSION)) return;
    const latestStable = getLatestStableDbVersion(dbVersions);
    if (latestStable) {
      setValue(DATABASE_VERSION, latestStable, { shouldValidate: true });
    }
  }, [dbVersions, getValues, setValue]);
};
