import { useEffect, useRef } from 'react';
import { UseFormReturn } from 'react-hook-form';
import { useQuery } from 'react-query';
import { generateUniqueName } from '@app/redesign/helpers/utils';
import { GeneralSettingsProps } from '../steps/general-settings/dtos';
import {
  CLOUD,
  DATABASE_VERSION,
  PROVIDER_CONFIGURATION,
  UNIVERSE_NAME
} from '../fields/FieldNames';
import {
  CREATE_UNIVERSE_DB_VERSIONS_QUERY_KEY,
  CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
  fetchCreateUniverseDbVersions,
  fetchCreateUniverseProviders,
  getDefaultCloudType,
  getLatestStableDbVersion,
  resolveProviderOnCloudSync,
  transformDbVersionQueryData
} from './generalSettingsDefaults';

type GeneralSettingsForm = Pick<
  UseFormReturn<GeneralSettingsProps>,
  'setValue' | 'getValues' | 'watch'
>;

/**
 * Fills General Settings defaults from the shared provider + DB version queries.
 * Re-selects the first ready provider whenever CloudType changes, including
 * switching from a CloudType with an empty provider list to one that has providers.
 */
export const useGeneralSettingsDefaults = ({
  setValue,
  getValues,
  watch
}: GeneralSettingsForm): void => {
  const cloud = watch(CLOUD);
  const previousCloudRef = useRef<string | undefined>();

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
    if (!currentCloud) {
      currentCloud = getDefaultCloudType(providers);
      setValue(CLOUD, currentCloud);
    }

    const currentProvider = getValues(PROVIDER_CONFIGURATION);
    const { shouldApply, provider } = resolveProviderOnCloudSync({
      previousCloud: previousCloudRef.current,
      currentCloud,
      currentProvider,
      providers
    });
    previousCloudRef.current = currentCloud;

    if (shouldApply) {
      setValue(PROVIDER_CONFIGURATION, provider as GeneralSettingsProps['providerConfiguration'], {
        shouldValidate: true
      });
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
