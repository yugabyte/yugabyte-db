/* eslint-disable no-console */
/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle } from 'react';

import { FormProvider, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { yupResolver } from '@hookform/resolvers/yup';
import { ProviderConfigurationField } from '../../fields/provider-configuration/ProviderConfiguration';
import { DatabaseVersionField } from '../../fields/database-version/DatabaseVersion';
import { UniverseNameField } from '../../fields';
import { CloudField } from '../../fields/provider/ProviderSelect';
import { GeneralSettingsProps } from './dtos';
import { GeneralSettingsValidationSchema } from './ValidationSchema';

import { generateUniqueName } from '../../../../../helpers/utils';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import {
  CLOUD,
  DATABASE_VERSION,
  PROVIDER_CONFIGURATION,
  UNIVERSE_NAME,
  REGIONS_FIELD,
  RESILIENCE_TYPE,
  RESILIENCE_FORM_MODE,
  REPLICATION_FACTOR,
  FAULT_TOLERANCE_TYPE,
  NODE_COUNT,
  SINGLE_AVAILABILITY_ZONE
} from '../../fields/FieldNames';
import ShuffleIcon from '../../../../../assets/shuffle.svg';
import { ResilienceType, ResilienceFormMode, FaultToleranceType } from '../resilence-regions/dtos';

const CONTROL_WIDTH = '480px';

export const GeneralSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { generalSettings, resilienceAndRegionsSettings },
    { moveToNextPage, saveGeneralSettings, saveResilienceAndRegionsSettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.generalSettings' });
  const methods = useForm<GeneralSettingsProps>({
    resolver: yupResolver(GeneralSettingsValidationSchema(t)),
    defaultValues: generalSettings,
    mode: 'onChange'
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
          saveGeneralSettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {}
    }),
    []
  );

  const { errors } = methods.formState;
  const cloud = methods.watch('cloud');

  useEffect(() => {
    methods.resetField(PROVIDER_CONFIGURATION);
    saveResilienceAndRegionsSettings({
      [REGIONS_FIELD]: [],
      [SINGLE_AVAILABILITY_ZONE]: '',
      [RESILIENCE_TYPE]: resilienceAndRegionsSettings?.[RESILIENCE_TYPE] ?? ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]:
        resilienceAndRegionsSettings?.[RESILIENCE_FORM_MODE] ?? ResilienceFormMode.GUIDED,
      [REPLICATION_FACTOR]: resilienceAndRegionsSettings?.[REPLICATION_FACTOR] ?? 3,
      [FAULT_TOLERANCE_TYPE]:
        resilienceAndRegionsSettings?.[FAULT_TOLERANCE_TYPE] ?? FaultToleranceType.AZ_LEVEL,
      [NODE_COUNT]: resilienceAndRegionsSettings?.[NODE_COUNT] ?? 1
    });
  }, [cloud]);

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
          <CloudField<GeneralSettingsProps> name={CLOUD} label={t('cloudProvider')} />
          <ProviderConfigurationField<GeneralSettingsProps>
            name={PROVIDER_CONFIGURATION}
            label={t('providerconfiguration')}
            placeholder={t('providerConfigurationPlaceholder')}
            sx={{
              width: CONTROL_WIDTH
            }}
            filterByProvider={cloud}
            dataTestId="provider-configuration-field"
          />
          <DatabaseVersionField<GeneralSettingsProps>
            name={DATABASE_VERSION}
            label={t('databaseVersion')}
            placeholder={t('databaseVersionPlaceholder')}
            sx={{
              width: CONTROL_WIDTH
            }}
            dataTestId="database-version-field"
          />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

GeneralSettings.displayName = 'GeneralSettings';
