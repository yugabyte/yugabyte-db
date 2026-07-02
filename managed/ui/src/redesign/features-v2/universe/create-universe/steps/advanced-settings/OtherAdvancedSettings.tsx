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
import { yupResolver } from '@hookform/resolvers/yup';
import { FormProvider, useForm } from 'react-hook-form';
import { mui, YBAccordion } from '@yugabyte-ui-library/core';
import { StyledInputWrapper } from '../../components/DefaultComponents';
import {
  DeploymentPortsField,
  UserTagsField,
  InstanceARNField,
  AccessKeyField
} from '../../fields';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { OtherAdvancedProps } from './dtos';
import { OtherAdvancedValidationSchema } from '@app/redesign/features-v2/universe/create-universe/steps/advanced-settings/ValidationSchema';

const { Box, Typography } = mui;

export const OtherAdvancedSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { generalSettings, databaseSettings, otherAdvancedSettings },
    { moveToNextPage, moveToPreviousPage, saveOtherAdvancedSettings }
  ] = useContext(CreateUniverseContext) as unknown as CreateUniverseContextMethods;

  const provider = generalSettings?.providerConfiguration;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings'
  });

  const methods = useForm<OtherAdvancedProps>({
    resolver: yupResolver(OtherAdvancedValidationSchema(t, provider?.code)),
    defaultValues: {
      ...(provider?.code !== CloudType.kubernetes && {
        instanceTags: [
          {
            name: '',
            value: ''
          }
        ]
      }),
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
        <YBAccordion
          titleContent={t('nodeAcessHeader')}
          sx={{ width: '100%', gap: '24px' }}
          defaultExpanded={true}
        >
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
            {provider?.code !== CloudType.kubernetes && (
              <StyledInputWrapper>
                <Typography variant="body1">{t('accessHeader')}</Typography>
                <AccessKeyField
                  disabled={false}
                  provider={generalSettings?.providerConfiguration?.uuid ?? ''}
                />
              </StyledInputWrapper>
            )}
            {provider?.code === CloudType.aws && (
              <StyledInputWrapper>
                <Typography variant="body1">{t('permissions')}</Typography>
                <InstanceARNField disabled={false} />
              </StyledInputWrapper>
            )}
          </Box>
        </YBAccordion>
        {provider && [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code) && (
          <YBAccordion titleContent={t('userTagsHeader')} sx={{ width: '100%' }}>
            <UserTagsField disabled={false} />
          </YBAccordion>
        )}
        {provider &&
        provider?.code !== CloudType.kubernetes &&
        databaseSettings?.ysql &&
        databaseSettings?.ycql ? (
          <YBAccordion titleContent={t('portsOverrideHeader')} sx={{ width: '100%' }}>
            <DeploymentPortsField
              disabled={false}
              providerCode={generalSettings?.providerConfiguration?.code as string}
              ysql={Boolean(databaseSettings?.ysql?.enable)}
              ycql={Boolean(databaseSettings?.ycql?.enable)}
              enableConnectionPooling={databaseSettings?.enableConnectionPooling}
            />
          </YBAccordion>
        ) : (
          <></>
        )}
      </Box>
      {/* <StyledPanel>
        <StyledHeader>{t('additionalSettingsHeader')}</StyledHeader>
        <StyledContent sx={{ gap: '16px' }}>
          //Need this section for k8s Helm overrides
        </StyledContent>
      </StyledPanel> */}
    </FormProvider>
  );
});

OtherAdvancedSettings.displayName = 'OtherAdvancedSettings';
