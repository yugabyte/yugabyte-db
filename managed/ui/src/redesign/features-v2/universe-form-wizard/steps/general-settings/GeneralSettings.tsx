/* eslint-disable no-console */
/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle } from 'react';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { UniverseNameField } from '../../fields';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { GeneralSettingsValidationSchema } from './ValidationSchema';
import { useTranslation } from 'react-i18next';
import { ProviderConfigurationField } from '../../fields/provider-configuration/ProviderConfiguration';
import { DatabaseVersionField } from '../../fields/database-version/DatabaseVersion';
import { CloudField } from '../../fields/provider/ProviderSelect';
import { GeneralSettingsProps } from './dtos';

import { ReactComponent as ShuffleIcon } from '../../../../assets/shuffle.svg';
import { generateUniqueName } from '../../../../helpers/utils';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';

const CONTROL_WIDTH = '480px';

export const GeneralSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [{ generalSettings }, { moveToNextPage, saveGeneralSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.generalSettings' });
  const methods = useForm<GeneralSettingsProps>({
    resolver: yupResolver(GeneralSettingsValidationSchema(t)),
    defaultValues: generalSettings
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        methods.handleSubmit((data) => {
          saveGeneralSettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {}
    }),
    []
  );

  const { errors } = methods.formState;

  return (
    <FormProvider {...methods}>
      <StyledPanel>
        <StyledHeader>{t('title')}</StyledHeader>
        <StyledContent>
          <div style={{ display: 'flex', gap: '16px', alignItems: 'center' }}>
            <UniverseNameField<GeneralSettingsProps>
              name="universeName"
              label={t('universeName')}
              placeholder={t('universeNamePlaceholder')}
              sx={{
                width: CONTROL_WIDTH
              }}
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
          <CloudField<GeneralSettingsProps> name="cloud" label={t('cloudProvider')} />
          <ProviderConfigurationField<GeneralSettingsProps>
            name="providerConfiguration"
            label={t('providerconfiguration')}
            placeholder={t('providerConfigurationPlaceholder')}
            sx={{
              width: CONTROL_WIDTH
            }}
          />
          <DatabaseVersionField<GeneralSettingsProps>
            name="databaseVersion"
            label={t('databaseVersion')}
            placeholder={t('databaseVersionPlaceholder')}
            sx={{
              width: CONTROL_WIDTH
            }}
          />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

GeneralSettings.displayName = 'GeneralSettings';
