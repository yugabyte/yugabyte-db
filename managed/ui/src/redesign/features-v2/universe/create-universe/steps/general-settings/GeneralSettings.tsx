/* eslint-disable no-console */
/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle, useRef } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { yupResolver } from '@hookform/resolvers/yup';
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
import { GeneralSettingsValidationSchema } from './ValidationSchema';
import { GeneralSettingsProps } from './dtos';
import {
  CLOUD,
  DATABASE_VERSION,
  PROVIDER_CONFIGURATION,
  UNIVERSE_NAME
} from '../../fields/FieldNames';
import ShuffleIcon from '../../../../../assets/shuffle.svg';
import { StyledPanel, StyledHeader, StyledContent } from '../../components/DefaultComponents';
import { Box } from '@material-ui/core';

const CONTROL_WIDTH = '480px';

export const GeneralSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { generalSettings },
    {
      moveToNextPage,
      saveGeneralSettings,
      saveResilienceAndRegionsSettings,
      saveNodesAvailabilitySettings,
      saveInstanceSettings
    }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.generalSettings' });
  const methods = useForm<GeneralSettingsProps>({
    resolver: yupResolver(GeneralSettingsValidationSchema(t)),
    defaultValues: generalSettings,
    mode: 'onChange'
  });

  usePersistStepFormValues(methods.watch, methods.getValues, saveGeneralSettings);

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
  const cloud = methods.watch('cloud');
  const providerConfiguration = methods.watch(PROVIDER_CONFIGURATION);
  const previousProviderUuidRef = useRef(providerConfiguration?.uuid);

  useEffect(() => {
    const providerUuid = providerConfiguration?.uuid;
    const previousProviderUuid = previousProviderUuidRef.current;
    previousProviderUuidRef.current = providerUuid;

    // Selecting the initial provider is not a provider change. This also prevents
    // returning to this step from clearing the previously configured placement.
    if (!previousProviderUuid || previousProviderUuid === providerUuid) return;

    resetProviderDependentSettings({
      saveResilienceAndRegionsSettings,
      saveNodesAvailabilitySettings,
      saveInstanceSettings
    });
  }, [providerConfiguration?.uuid]);

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
          <CloudField<GeneralSettingsProps> name={CLOUD} label={t('cloudProvider')} />
          </Box>
          {cloud && (
            <ProviderConfigurationField<GeneralSettingsProps>
              name={PROVIDER_CONFIGURATION}
              label={t('providerconfiguration')}
              placeholder={t('providerConfigurationPlaceholder')}
              sx={{
                width: CONTROL_WIDTH
              }}
              filterByProvider={cloud}
              dataTestId="provider-configuration-field"
              disabled={!cloud}
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
            disabled={!cloud}
          />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

GeneralSettings.displayName = 'GeneralSettings';
