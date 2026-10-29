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
import { useQueries } from 'react-query';
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
import { ACCESS_KEY_CODE_BY_PROVIDER, USER_TAGS_FIELD } from '../../fields/FieldNames';
import { OtherAdvancedValidationSchema } from '@app/redesign/features-v2/universe/create-universe/steps/advanced-settings/ValidationSchema';
import { DEFAULT_COMMUNICATION_PORTS } from '../../helpers/constants';
import { canOverrideCommunicationPorts } from '../../helpers/syncConnectionPoolingPorts';
import { COMMUNICATION_PORT_FIELD_NAMES } from '../../helpers/duplicatePortValidation';
import {
  getPrimaryProvider,
  getProviderConfigurations
} from '../../helpers/providerConfigurations';
import { useRuntimeConfigValues } from '../../helpers/utils';

const { Box, Typography } = mui;

export const OtherAdvancedSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [context, { moveToNextPage, moveToPreviousPage, saveOtherAdvancedSettings }] = useContext(
    CreateUniverseContext
  ) as unknown as CreateUniverseContextMethods;

  const { generalSettings, databaseSettings, otherAdvancedSettings } = context;

  const placementSpec = constructPlacements({ ...context });

  const providers = getProviderConfigurations(generalSettings);
  const provider = getPrimaryProvider(generalSettings);
  const dbVersion = generalSettings?.databaseVersion;

  const sshProviders = useMemo(
    () => providers.filter((p) => p.code !== CloudType.kubernetes),
    [providers]
  );
  const isMultiSsh = sshProviders.length >= 2;
  const showNodeAccessCard = sshProviders.length > 0;
  const showAwsArn = providers.some((p) => p.code === CloudType.aws);

  const accessKeyQueries = useQueries(
    sshProviders.map((p) => ({
      queryKey: [QUERY_KEY.getAccessKeys, p.uuid],
      queryFn: () => api.getAccessKeys(p.uuid),
      enabled: !!p.uuid
    }))
  );

  const accessKeyQueryFingerprint = accessKeyQueries
    .map((q) => `${q.status}:${q.data?.length ?? 0}`)
    .join('|');

  const providersRequiringAccessKey = useMemo(
    () =>
      sshProviders
        .filter((_, i) => (accessKeyQueries[i]?.data?.length ?? 0) > 0)
        .map((p) => p.uuid),
    // eslint-disable-next-line react-hooks/exhaustive-deps -- fingerprint tracks query results
    [sshProviders, accessKeyQueryFingerprint]
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings'
  });

  const validationSchema = useMemo(
    () =>
      OtherAdvancedValidationSchema(t, {
        providerCode: provider?.code,
        providersRequiringAccessKey,
        ysql: !!databaseSettings?.ysql?.enable,
        ycql: !!databaseSettings?.ycql?.enable,
        enableConnectionPooling: !!databaseSettings?.enableConnectionPooling
      }),
    [
      t,
      provider?.code,
      providersRequiringAccessKey,
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
      accessKeyCodeByProvider: {},
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
    setValue,
    getValues,
    formState: { errors, isSubmitted }
  } = methods;

  // Drop access-key map entries for providers no longer selected.
  useEffect(() => {
    const allowed = new Set(sshProviders.map((p) => p.uuid));
    const map = getValues(ACCESS_KEY_CODE_BY_PROVIDER) ?? {};
    const next: Record<string, string> = {};
    let changed = false;
    for (const [uuid, code] of Object.entries(map)) {
      if (allowed.has(uuid)) {
        next[uuid] = code;
      } else {
        changed = true;
      }
    }
    if (changed) {
      setValue(ACCESS_KEY_CODE_BY_PROVIDER, next, { shouldValidate: true });
    }
  }, [sshProviders, getValues, setValue]);

  const [showErrorsAfterSubmit, setShowErrorsAfterSubmit] = useState(false);
  const [portsAccordionExpanded, setPortsAccordionExpanded] = useState(false);
  const hasErrors = !isEmpty(errors);
  const hasPortErrors = COMMUNICATION_PORT_FIELD_NAMES.some(
    (fieldName) => !!(errors as Record<string, unknown>)[fieldName]
  );

  const userTagsValue = watch(USER_TAGS_FIELD);
  // Any row (even an invalid one kept via Back) must show, or its inline errors would be hidden.
  const hasUserTagRow = !!userTagsValue?.length;
  const watched = watch();

  const { runtimeConfigs, devUserTags } = useRuntimeConfigValues();
  const [devTagsApplied, setDevTagsApplied] = useState(false);
  // Seed once per create flow so tags the user removed are not re-added on revisit.
  useEffect(() => {
    if (!isCloudVendorCloudType(provider?.code) || !runtimeConfigs) return;
    if (methods.getValues('userTagsSeeded')) return;
    if (devUserTags.length && !methods.getValues(USER_TAGS_FIELD)?.some((tag) => tag.name)) {
      methods.setValue(USER_TAGS_FIELD, devUserTags);
      setDevTagsApplied(true);
    }
    methods.setValue('userTagsSeeded', true);
  }, [runtimeConfigs, devUserTags, provider?.code, methods]);

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
                <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
                  {sshProviders.map((sshProvider) => (
                    <AccessKeyField
                      key={sshProvider.uuid}
                      disabled={false}
                      provider={sshProvider.uuid}
                      providerCode={sshProvider.code}
                      showProviderLogo={isMultiSsh}
                      showHelperText={!isMultiSsh}
                    />
                  ))}
                  {isMultiSsh && (
                    <Typography
                      sx={{
                        fontSize: '12px',
                        lineHeight: '16px',
                        color: '#6D7C88',
                        maxWidth: '560px'
                      }}
                    >
                      {t('accessKeyHelper')}
                    </Typography>
                  )}
                </Box>
              </StyledInputWrapper>

              {showAwsArn && (
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
            // Remount once on seeding so tags that arrive after the first render expand it.
            key={devTagsApplied ? 'user-tags-seeded' : 'user-tags'}
            titleContent={t('userTagsHeader')}
            sx={{ width: '100%' }}
            defaultExpanded={hasUserTagRow}
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
                providerCode={getPrimaryProvider(generalSettings)?.code as string}
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
