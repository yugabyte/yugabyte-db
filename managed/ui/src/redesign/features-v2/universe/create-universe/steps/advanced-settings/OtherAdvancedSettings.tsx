/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { mui, YBAccordion } from '@yugabyte-ui-library/core';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import {
  DeploymentPortsField,
  UserTagsField,
  TimeSyncField,
  InstanceARNField,
  SystemDField,
  AccessKeyField
} from '../../fields';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { OtherAdvancedProps } from './dtos';

const { Box } = mui;

export const OtherAdvancedSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { generalSettings, databaseSettings, otherAdvancedSettings },
    { moveToNextPage, moveToPreviousPage, saveOtherAdvancedSettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const provider = generalSettings?.providerConfiguration;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings'
  });

  const methods = useForm<OtherAdvancedProps>({
    defaultValues: {
      instanceTags: [
        {
          name: '',
          value: ''
        }
      ],
      ...otherAdvancedSettings
    },
    mode: 'onChange'
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
          saveOtherAdvancedSettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    []
  );

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', gap: '24px' }}>
        <YBAccordion titleContent={t('portsOverrideHeader')} sx={{ width: '100%' }}>
          {provider &&
          provider?.code !== CloudType.kubernetes &&
          databaseSettings?.ysql &&
          databaseSettings?.ycql ? (
            <DeploymentPortsField
              disabled={false}
              providerCode={generalSettings?.providerConfiguration?.code}
              ysql={databaseSettings?.ysql}
              ycql={databaseSettings?.ycql}
              enableConnectionPooling={databaseSettings?.enableConnectionPooling}
            />
          ) : (
            <></>
          )}
        </YBAccordion>
        {provider?.code !== CloudType.kubernetes && (
          <YBAccordion titleContent={t('userTagsHeader')} sx={{ width: '100%' }}>
            <UserTagsField disabled={false} />
          </YBAccordion>
        )}
      </Box>
      <StyledPanel>
        <StyledHeader>{t('additionalSettingsHeader')}</StyledHeader>
        <StyledContent>
          {provider && provider?.code !== CloudType.kubernetes && (
            <TimeSyncField disabled={false} provider={provider} />
          )}
          {provider?.code !== CloudType.kubernetes && (
            <AccessKeyField
              disabled={false}
              provider={generalSettings?.providerConfiguration?.uuid ?? ''}
            />
          )}
          {provider?.code === CloudType.aws && <InstanceARNField disabled={false} />}
          {provider?.code !== CloudType.kubernetes && <SystemDField disabled={false} />}
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

OtherAdvancedSettings.displayName = 'OtherAdvancedSettings';
