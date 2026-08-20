/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle, useMemo, useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { isEmpty } from 'lodash';
import { yupResolver } from '@hookform/resolvers/yup';
import { FormProvider, useForm } from 'react-hook-form';
import { AlertVariant, mui, YBAccordion, YBAlert } from '@yugabyte-ui-library/core';
import { StyledInputWrapper } from '../../components/DefaultComponents';
import {
  DeploymentPortsField,
  UserTagsField,
  InstanceARNField,
  AccessKeyField,
  K8sHelmOverridesCard
} from '../../fields';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { usePersistStepFormValues } from '../../helpers/persistStepFormValues';
import { constructPlacements } from '../../utils/createUniversePayload';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import { isCloudVendorCloudType } from '@app/components/configRedesign/providerRedesign/utils';
import { OtherAdvancedProps } from './dtos';
import { USER_TAGS_FIELD } from '../../fields/FieldNames';
import { OtherAdvancedValidationSchema } from '@app/redesign/features-v2/universe/create-universe/steps/advanced-settings/ValidationSchema';
import { DEFAULT_COMMUNICATION_PORTS } from '../../helpers/constants';
import { canOverrideCommunicationPorts } from '../../helpers/syncConnectionPoolingPorts';
import { COMMUNICATION_PORT_FIELD_NAMES } from '../../helpers/duplicatePortValidation';

const { Box, Typography } = mui;

export const OtherAdvancedSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [context, { moveToNextPage, moveToPreviousPage, saveOtherAdvancedSettings }] = useContext(
    CreateUniverseContext
  ) as unknown as CreateUniverseContextMethods;

  const { generalSettings, databaseSettings, otherAdvancedSettings } = context;

  const placementSpec = constructPlacements({ ...context });

  const provider = generalSettings?.providerConfiguration;
  const dbVersion = generalSettings?.databaseVersion;
  const isOnPrem = provider?.code === CloudType.onprem;

  // On-prem providers (e.g. manually provisioned). Hide the Node Access
  const { data: accessKeys, isLoading: isAccessKeysLoading } = useQuery(
    [QUERY_KEY.getAccessKeys, provider?.uuid],
    () => api.getAccessKeys(provider?.uuid),
    { enabled: !!provider?.uuid && isOnPrem }
  );
  const hasSshAccessKeys = (accessKeys?.length ?? 0) > 0;
  const showNodeAccessCard =
    !!provider &&
    provider.code !== CloudType.kubernetes &&
    !(isOnPrem && (isAccessKeysLoading || !hasSshAccessKeys));

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings'
  });

  const validationSchema = useMemo(
    () =>
      OtherAdvancedValidationSchema(t, {
        providerCode: provider?.code,
        requireAccessKey: showNodeAccessCard,
        ysql: !!databaseSettings?.ysql?.enable,
        ycql: !!databaseSettings?.ycql?.enable,
        enableConnectionPooling: !!databaseSettings?.enableConnectionPooling
      }),
    [
      t,
      provider?.code,
      showNodeAccessCard,
      databaseSettings?.ysql?.enable,
      databaseSettings?.ycql?.enable,
      databaseSettings?.enableConnectionPooling
    ]
  );

  const methods = useForm<OtherAdvancedProps>({
    resolver: yupResolver(validationSchema),
    defaultValues: {
      ...DEFAULT_COMMUNICATION_PORTS,
      instanceTags: [],
      awsArnString: '',
      useSystemd: true,
      accessKeyCode: '',
      universeOverrides: '',
      azOverrides: {},
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

  usePersistStepFormValues(methods.watch, methods.getValues, saveOtherAdvancedSettings);

  const {
    watch,
    trigger,
    formState: { errors, isSubmitted }
  } = methods;

  const [showErrorsAfterSubmit, setShowErrorsAfterSubmit] = useState(false);
  const [portsAccordionExpanded, setPortsAccordionExpanded] = useState(false);
  const hasErrors = !isEmpty(errors);
  const hasPortErrors = COMMUNICATION_PORT_FIELD_NAMES.some(
    (fieldName) => !!(errors as Record<string, unknown>)[fieldName]
  );

  const userTagsValue = watch(USER_TAGS_FIELD);
  const watched = watch();

  useEffect(() => {
    if (isSubmitted && !hasErrors) {
      setShowErrorsAfterSubmit(false);
    }
  }, [isSubmitted, hasErrors]);

  useEffect(() => {
    if (isSubmitted) trigger();
  }, [JSON.stringify(watched), isSubmitted, trigger]);

  useEffect(() => {
    if (showErrorsAfterSubmit && hasPortErrors) {
      setPortsAccordionExpanded(true);
    }
  }, [showErrorsAfterSubmit, hasPortErrors]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        setShowErrorsAfterSubmit(true);
        return methods.handleSubmit(() => {
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    [methods, moveToNextPage, moveToPreviousPage]
  );

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', gap: '24px' }}>
        {showNodeAccessCard && (
          <YBAccordion
            titleContent={t('nodeAcessHeader')}
            sx={{ width: '100%', gap: '24px' }}
            defaultExpanded={true}
          >
            <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
              <StyledInputWrapper>
                <Typography variant="body1">{t('accessHeader')}</Typography>
                <AccessKeyField
                  disabled={false}
                  provider={generalSettings?.providerConfiguration?.uuid ?? ''}
                />
              </StyledInputWrapper>

              {provider?.code === CloudType.aws && (
                <StyledInputWrapper>
                  <Typography variant="body1">{t('permissions')}</Typography>
                  <InstanceARNField disabled={false} />
                </StyledInputWrapper>
              )}
            </Box>
          </YBAccordion>
        )}
        {provider && isCloudVendorCloudType(provider?.code) && (
          <YBAccordion
            titleContent={t('userTagsHeader')}
            sx={{ width: '100%' }}
            defaultExpanded={userTagsValue?.length > 1 ? true : false}
          >
            <UserTagsField disabled={false} />
          </YBAccordion>
        )}
        {provider &&
          canOverrideCommunicationPorts(provider.code) &&
          databaseSettings?.ysql &&
          databaseSettings?.ycql && (
            <YBAccordion
              titleContent={t('portsOverrideHeader')}
              sx={{ width: '100%' }}
              expanded={portsAccordionExpanded}
              onChange={(_, expanded) => setPortsAccordionExpanded(expanded)}
            >
              <DeploymentPortsField
                providerCode={generalSettings?.providerConfiguration?.code as string}
                ysql={!!databaseSettings?.ysql?.enable}
                ycql={!!databaseSettings?.ycql?.enable}
                enableConnectionPooling={databaseSettings?.enableConnectionPooling}
              />
            </YBAccordion>
          )}
      </Box>
      {provider?.code === CloudType.kubernetes && (
        <YBAccordion titleContent={t('k8sOverrides')} sx={{ width: '100%' }} defaultExpanded={true}>
          <K8sHelmOverridesCard placementSpec={placementSpec} dbVersion={dbVersion ?? ''} />
        </YBAccordion>
      )}
      {showErrorsAfterSubmit && hasErrors && (
        <Box>
          <YBAlert
            open
            variant={AlertVariant.Error}
            text={<Trans t={t}>{t('validation.alertMsg')}</Trans>}
          />
        </Box>
      )}
    </FormProvider>
  );
});

OtherAdvancedSettings.displayName = 'OtherAdvancedSettings';
