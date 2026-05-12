import { forwardRef, useContext, useImperativeHandle, useState } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { mui, YBAlert, AlertVariant } from '@yugabyte-ui-library/core';
import { AssignPublicIPField, EARField, EITField, K8EITField, IPV6Field } from '../../fields';
import { StyledPanel, StyledHeader, StyledContent } from '../../components/DefaultComponents';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { SecuritySettingsProps } from './dtos';
import { useUpdateEffect } from 'react-use';
import {
  NTON_CERT_FIELD,
  CTON_CERT_FIELD,
  COMMON_CERT_FIELD,
  KMS_CONFIG_FIELD
} from '../../fields/FieldNames';

const { Box } = mui;

export const SecuritySettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { securitySettings, generalSettings },
    { moveToNextPage, moveToPreviousPage, saveSecuritySettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const provider = generalSettings?.providerConfiguration;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.securitySettings'
  });

  const methods = useForm<SecuritySettingsProps>({
    defaultValues: { ...securitySettings },
    mode: 'onChange'
  });

  const { trigger, formState, watch } = methods;
  const [showErrorsAfterSubmit, setShowErrorsAfterSubmit] = useState(false);
  const { errors, isSubmitted } = formState;

  const nTonCertVal = watch(NTON_CERT_FIELD);
  const cTonCertVal = watch(CTON_CERT_FIELD);
  const commonCertVal = watch(COMMON_CERT_FIELD);
  const kmsConfigVal = watch(KMS_CONFIG_FIELD);

  useUpdateEffect(() => {
    if (isSubmitted) {
      trigger().then((isValid) => {
        if (isValid) setShowErrorsAfterSubmit(false);
      });
    }
  }, [nTonCertVal, cTonCertVal, commonCertVal, kmsConfigVal]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        setShowErrorsAfterSubmit(true);
        return methods.handleSubmit((data) => {
          saveSecuritySettings(data);
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
        <StyledPanel>
          <StyledHeader>{t('networkAcessTitle')}</StyledHeader>
          <StyledContent>
            {provider?.code !== CloudType.kubernetes && (
              <AssignPublicIPField
                disabled={false}
                providerCode={generalSettings?.providerConfiguration?.code ?? ''}
              />
            )}
            {provider?.code === CloudType.kubernetes && <IPV6Field disabled={false} />}
          </StyledContent>
        </StyledPanel>
        <StyledPanel>
          <StyledHeader>{t('eitTitle')}</StyledHeader>
          <StyledContent>
            {provider?.code !== CloudType.kubernetes ? (
              <EITField disabled={false} />
            ) : (
              <K8EITField disabled={false} />
            )}
          </StyledContent>
        </StyledPanel>
        <StyledPanel>
          <StyledHeader>{t('earTitle')}</StyledHeader>
          <StyledContent>
            <EARField disabled={false} />
          </StyledContent>
        </StyledPanel>
      </Box>
      {showErrorsAfterSubmit && errors && (
        <Box>
          <YBAlert open variant={AlertVariant.Error} text={<Trans t={t}>{t('alertMsg')}</Trans>} />
        </Box>
      )}
    </FormProvider>
  );
});

SecuritySettings.displayName = 'SecuritySettings';
