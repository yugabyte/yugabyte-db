/* eslint-disable no-console */
/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle, useMemo, useRef } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { yupResolver } from '@hookform/resolvers/yup';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import {
  UniverseNameField,
  ProviderConfigurationField,
  DatabaseVersionField,
  CloudField
} from '../../fields';
import { generateUniqueName } from '../../../../../helpers/utils';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import {
  resetProviderDependentSettings,
  usePersistStepFormValues
} from '../../helpers/persistStepFormValues';
import { useGeneralSettingsDefaults } from '../../helpers/useGeneralSettingsDefaults';
import {
  buildGeneralSettingsDefaults,
  CREATE_UNIVERSE_DB_VERSIONS_QUERY_KEY,
  CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
  fetchCreateUniverseDbVersions,
  fetchCreateUniverseProviders,
  transformDbVersionQueryData
} from '../../helpers/generalSettingsDefaults';
import { GeneralSettingsValidationSchema } from './ValidationSchema';
import { GeneralSettingsProps } from './dtos';
import {
  CLOUD,
  DATABASE_VERSION,
  PROVIDER_CONFIGURATIONS,
  UNIVERSE_NAME
} from '../../fields/FieldNames';
import ShuffleIcon from '../../../../../assets/shuffle.svg';
import { StyledPanel, StyledHeader, StyledContent } from '../../components/DefaultComponents';
import { Box } from '@material-ui/core';
import {
  cloudCodes,
  crossedProviderCountBoundary,
  ENABLE_MULTI_PROVIDER_CREATE,
  includesKubernetesCloud
} from '../../helpers/providerConfigurations';
import { providerUuidSetKey } from '../../helpers/instanceByProvider';

const CONTROL_WIDTH = '480px';

export const GeneralSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { generalSettings },
    {
      moveToNextPage,
      saveGeneralSettings,
      saveResilienceAndRegionsSettings,
      saveNodesAvailabilitySettings,
      saveInstanceSettings,
      saveInstanceByProvider,
      clearProviderSlots
    }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.generalSettings' });

  const { data: providers } = useQuery(
    CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
    fetchCreateUniverseProviders
  );
  const { data: dbVersions } = useQuery(
    CREATE_UNIVERSE_DB_VERSIONS_QUERY_KEY,
    fetchCreateUniverseDbVersions,
    { select: transformDbVersionQueryData }
  );

  const defaultValues = useMemo(
    () => buildGeneralSettingsDefaults(providers, dbVersions, generalSettings),
    // First paint only: providers/DB versions are already in the React Query cache from CreateUniverse.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    []
  );

  const methods = useForm<GeneralSettingsProps>({
    resolver: yupResolver(GeneralSettingsValidationSchema(t)),
    defaultValues,
    mode: 'onChange'
  });

  usePersistStepFormValues(methods.watch, methods.getValues, saveGeneralSettings);
  useGeneralSettingsDefaults(methods);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit(() => {
          moveToNextPage();
        })();
      },
      onPrev: () => {}
    }),
    []
  );

  const { errors } = methods.formState;
  const cloud = methods.watch(CLOUD);
  // Library toggle stores multi mode as a cloud array.
  const multiUi = ENABLE_MULTI_PROVIDER_CREATE && Array.isArray(cloud);
  const selectedClouds = cloudCodes(cloud);
  const providerConfigurations = methods.watch(PROVIDER_CONFIGURATIONS);
  const primaryProviderUuid = providerConfigurations?.[0]?.uuid;
  const providerCount = providerConfigurations?.length ?? 0;
  const previousPrimaryUuidRef = useRef(primaryProviderUuid);
  const previousProviderCountRef = useRef(providerCount);
  const providerSetKey = providerUuidSetKey(providerConfigurations);
  const previousProviderSetKeyRef = useRef(providerSetKey);
  useEffect(() => {
    const previousPrimaryUuid = previousPrimaryUuidRef.current;
    const previousCount = previousProviderCountRef.current;
    previousPrimaryUuidRef.current = primaryProviderUuid;
    previousProviderCountRef.current = providerCount;

    const crossedBoundary = crossedProviderCountBoundary(previousCount, providerCount);
    // N=1: selecting the initial provider is not a change. Returning to this step must not
    // clear placement. Multi: crossing 1↔2+ always clears Placement/Nodes/Hardware + stubs.
    const primaryChangedWhileSingle =
      previousCount <= 1 &&
      providerCount <= 1 &&
      !!previousPrimaryUuid &&
      previousPrimaryUuid !== primaryProviderUuid;

    if (!crossedBoundary && !primaryChangedWhileSingle) return;

    resetProviderDependentSettings({
      saveResilienceAndRegionsSettings,
      saveNodesAvailabilitySettings,
      saveInstanceSettings,
      clearProviderSlots
    });
  }, [primaryProviderUuid, providerCount]);

  // Stay in multi: any uuid-set change wipes HW only (placements/nodes keep their own rules).
  useEffect(() => {
    const previousKey = previousProviderSetKeyRef.current;
    previousProviderSetKeyRef.current = providerSetKey;
    if (providerCount < 2 || previousKey === providerSetKey) return;
    // 1↔2+ already cleared via clearProviderSlots above; skip first paint / empty→empty.
    if (!previousKey) return;
    saveInstanceByProvider({});
  }, [providerSetKey, providerCount, saveInstanceByProvider]);

  // The library span toggle cannot be disabled. Kubernetes stays single-cloud.
  useEffect(() => {
    if (!Array.isArray(cloud) || !includesKubernetesCloud(cloud)) return;
    const rest = cloud.filter((code) => code && code !== CloudType.kubernetes);
    methods.setValue(CLOUD, rest.length ? rest : cloud.find(Boolean) ?? '', {
      shouldValidate: true
    });
  }, [cloud, methods]);

  return (
    <FormProvider {...methods}>
      <StyledPanel>
        <StyledHeader>{t('title')}</StyledHeader>
        <StyledContent>
          <div style={{ display: 'flex', gap: '16px', alignItems: 'center' }}>
            <UniverseNameField<GeneralSettingsProps>
              name={UNIVERSE_NAME}
              label={t('universeName')}
              placeholder={t('universeNamePlaceholder')}
              sx={{
                width: CONTROL_WIDTH
              }}
              dataTestId="universe-name-field"
            />
            <ShuffleIcon
              style={{
                marginTop: errors?.universeName?.message ? '0px' : '20px',
                cursor: 'pointer'
              }}
              onClick={() => {
                methods.setValue('universeName', generateUniqueName());
              }}
            />
          </div>
          <Box maxWidth={740}>
            <CloudField<GeneralSettingsProps>
              name={CLOUD}
              label={t('cloudProvider')}
              showMultiToggle={ENABLE_MULTI_PROVIDER_CREATE}
              multiToggleLabel={t('spanAcrossProviders')}
              disableKubernetes={multiUi}
            />
          </Box>
          {multiUi
            ? selectedClouds.map((code) => (
                <ProviderConfigurationField<GeneralSettingsProps>
                  key={code}
                  name={PROVIDER_CONFIGURATIONS}
                  label={t('providerConfigurationForCloud', { cloud: code.toUpperCase() })}
                  placeholder={t('providerConfigurationPlaceholder')}
                  sx={{ width: CONTROL_WIDTH }}
                  filterByProvider={code}
                  dataTestId={`provider-configuration-field-${code}`}
                />
              ))
            : !!selectedClouds[0] && (
                <ProviderConfigurationField<GeneralSettingsProps>
                  name={PROVIDER_CONFIGURATIONS}
                  label={t('providerconfiguration')}
                  placeholder={t('providerConfigurationPlaceholder')}
                  sx={{ width: CONTROL_WIDTH }}
                  filterByProvider={selectedClouds[0]}
                  dataTestId="provider-configuration-field"
                  disabled={!selectedClouds[0]}
                />
              )}
          <DatabaseVersionField<GeneralSettingsProps>
            name={DATABASE_VERSION}
            label={t('databaseVersion')}
            placeholder={t('databaseVersionPlaceholder')}
            sx={{
              width: CONTROL_WIDTH
            }}
            dataTestId="database-version-field"
            disabled={!selectedClouds.length}
          />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

GeneralSettings.displayName = 'GeneralSettings';
