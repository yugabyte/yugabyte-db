import { FC } from 'react';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { useTranslation, Trans } from 'react-i18next';
import { useFormContext, useWatch, Controller, FieldPath } from 'react-hook-form';
import {
  mui,
  YBToggleField,
  YBLabel,
  YBAutoComplete,
  YBCheckboxField,
  YBRadioGroupField
} from '@yugabyte-ui-library/core';
import { FieldContainer } from '../../components/DefaultComponents';
import { QUERY_KEY, api } from '../../../../../features/universe/universe-form/utils/api';
import { SecuritySettingsProps, CertType } from '../../steps/security-settings/dtos';
import {
  ENABLE_BOTH_ENCRYPTION,
  USE_SAME_CERT_FIELD,
  COMMON_CERT_FIELD,
  COMMON_CERT_TYPE_FIELD,
  ENABLE_NTON_FIELD,
  ENABLE_CTON_FIELD,
  NTON_CERT_FIELD,
  NTON_CERT_TYPE_FIELD,
  CTON_CERT_FIELD,
  CTON_CERT_TYPE_FIELD
} from '../FieldNames';

//icons
import NextLineIcon from '../../../../../assets/next-line.svg';

const { Box, Typography, styled } = mui;

interface EARProps {
  disabled: boolean;
}

interface CertCompProps {
  toggleFieldPath: FieldPath<SecuritySettingsProps>;
  certFieldPath: FieldPath<SecuritySettingsProps>;
  certTypePath: FieldPath<SecuritySettingsProps>;
}

const getOptionLabel = (option: any): string => option.label ?? '';

const CERT_OPTIONS = [
  {
    value: CertType.SELF_SIGNED,
    label: 'Use system-generated (self-signed) certificate'
  },
  {
    value: CertType.CUSTOM,
    label: 'Use customer-managed certificate'
  }
];

const CERTComponent: FC<CertCompProps> = ({ toggleFieldPath, certFieldPath, certTypePath }) => {
  const { control, setValue } = useFormContext<SecuritySettingsProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.securitySettings.eitField'
  });

  const isOptionEnabled = useWatch({ name: toggleFieldPath });
  const certTypeValue = useWatch({ name: certTypePath });

  //fetch data
  const { data: certificates = [], isLoading } = useQuery(
    QUERY_KEY.getCertificates,
    api.getCertificates
  );

  const handleChange = (e: any, option: any) => {
    setValue(certFieldPath, option?.uuid);
  };

  useUpdateEffect(() => {
    if (isOptionEnabled && !certTypeValue) setValue(certTypePath, CertType.SELF_SIGNED);
  }, [isOptionEnabled]);

  return (
    <FieldContainer sx={{ padding: '16px 24px' }}>
      <YBToggleField
        control={control}
        name={toggleFieldPath}
        label={t(toggleFieldPath)}
        dataTestId={`enable-encryption-in-transit-field`}
      />
      {isOptionEnabled && (
        <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', mt: 4, gap: '16px' }}>
          <Box sx={{ display: 'flex', flexDirection: 'row', gap: '16px', alignItems: 'center' }}>
            <NextLineIcon />
            <Typography variant="body2" sx={{ color: '#0B1117' }}>
              {t('certTypeLabel')}
            </Typography>
          </Box>
          <Box sx={{ display: 'flex', flexDirection: 'column', ml: 5, gap: '16px' }}>
            <YBRadioGroupField
              name={certTypePath}
              control={control}
              options={CERT_OPTIONS}
              dataTestId="certType"
              sx={{ gap: 2 }}
            />
            {certTypeValue === CertType.CUSTOM && (
              <Controller
                name={certFieldPath}
                control={control}
                rules={{ required: 'This field is required' }}
                render={({ field, fieldState }) => {
                  const value = certificates.find((i) => i.uuid === field.value) ?? '';
                  return (
                    <Box
                      sx={{ display: 'flex', flexDirection: 'column', width: '100%', pl: 4 }}
                      data-testid="RootCertificateField-Container"
                    >
                      <YBLabel error={!!fieldState.error}>{t('selectRootCert')}</YBLabel>
                      <Box
                        sx={{
                          display: 'flex',
                          width: '100%'
                        }}
                      >
                        <YBAutoComplete
                          loading={isLoading}
                          options={(certificates as unknown) as Record<string, string>[]}
                          getOptionLabel={getOptionLabel}
                          fullWidth={true}
                          onChange={handleChange}
                          value={(value as unknown) as never}
                          ybInputProps={{
                            placeholder: t('rootCertificatePlaceHolder'),
                            error: !!fieldState.error,
                            helperText: fieldState.error?.message,
                            dataTestId: 'root-certificate-field'
                          }}
                          dataTestId="root-certificate-field-container"
                          size="large"
                        />
                      </Box>
                    </Box>
                  );
                }}
              />
            )}
          </Box>
        </Box>
      )}
    </FieldContainer>
  );
};

export const EITField: FC<EARProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<SecuritySettingsProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.securitySettings.eitField'
  });

  const useSameCertValue = useWatch({ name: USE_SAME_CERT_FIELD });
  const enableBothValue = useWatch({ name: ENABLE_BOTH_ENCRYPTION });
  const nTonValue = useWatch({ name: ENABLE_NTON_FIELD });
  const cTonValue = useWatch({ name: ENABLE_CTON_FIELD });

  useUpdateEffect(() => {
    if (nTonValue || cTonValue) setValue(ENABLE_BOTH_ENCRYPTION, true);
  }, [useSameCertValue]);

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', gap: '16px' }}>
      <YBCheckboxField
        control={control}
        name={USE_SAME_CERT_FIELD}
        label={t('useSameCert')}
        size="large"
        dataTestId={`use-same-certificate-field`}
      />
      {useSameCertValue ? (
        <CERTComponent
          toggleFieldPath={ENABLE_BOTH_ENCRYPTION}
          certFieldPath={COMMON_CERT_FIELD}
          certTypePath={COMMON_CERT_TYPE_FIELD}
        />
      ) : (
        <>
          <CERTComponent
            toggleFieldPath={ENABLE_NTON_FIELD}
            certFieldPath={NTON_CERT_FIELD}
            certTypePath={NTON_CERT_TYPE_FIELD}
          />
          <CERTComponent
            toggleFieldPath={ENABLE_CTON_FIELD}
            certFieldPath={CTON_CERT_FIELD}
            certTypePath={CTON_CERT_TYPE_FIELD}
          />
        </>
      )}
    </Box>
  );
};

export const K8CertContainer = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  width: '100%',
  height: 'auto',
  backgroundColor: '#F7F9FB',
  border: '1px solid #D7DEE4',
  borderRadius: '8px',
  padding: '16px 16px 24px 16px',
  gap: '16px'
}));

export const K8EITField: FC<EARProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<SecuritySettingsProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.securitySettings.eitField'
  });

  //fetch data
  const { data: certificates = [], isLoading } = useQuery(
    QUERY_KEY.getCertificates,
    api.getCertificates
  );

  const enableEncryptionVal = useWatch({ name: ENABLE_BOTH_ENCRYPTION });
  const nToNVal = useWatch({ name: ENABLE_NTON_FIELD });
  const cToNVal = useWatch({ name: ENABLE_CTON_FIELD });
  const certTypeVal = useWatch({ name: COMMON_CERT_TYPE_FIELD });

  const handleChange = (e: any, option: any) => {
    setValue(COMMON_CERT_FIELD, option?.uuid);
  };

  useUpdateEffect(() => {
    if (enableEncryptionVal) {
      setValue(ENABLE_NTON_FIELD, true);
      setValue(ENABLE_CTON_FIELD, true);
      if (!certTypeVal) setValue(COMMON_CERT_TYPE_FIELD, CertType.SELF_SIGNED);
    }
  }, [enableEncryptionVal]);

  useUpdateEffect(() => {
    if (!nToNVal && !cToNVal) {
      setValue(ENABLE_BOTH_ENCRYPTION, false);
      setValue(COMMON_CERT_TYPE_FIELD, CertType.SELF_SIGNED);
      setValue(COMMON_CERT_FIELD, '');
    }
  }, [nToNVal, cToNVal]);

  return (
    <FieldContainer sx={{ padding: '16px 24px' }}>
      <YBToggleField
        control={control}
        name={ENABLE_BOTH_ENCRYPTION}
        label={t('eitLabel')}
        dataTestId={`enable-encryption-in-transit-field`}
      />
      {enableEncryptionVal && (
        <Box sx={{ display: 'flex', flexDirection: 'row', gap: '16px', mt: 4 }}>
          <NextLineIcon />
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
            <YBCheckboxField
              control={control}
              label={<Trans>{t('nodeToNodeLabel')}</Trans>}
              name={ENABLE_NTON_FIELD}
              size="large"
              dataTestId={'nTOn'}
            />
            <YBCheckboxField
              control={control}
              label={<Trans>{t('clientToNodeLabel')}</Trans>}
              name={ENABLE_CTON_FIELD}
              size="large"
              dataTestId={'cTOn'}
            />
            {(nToNVal || cToNVal) && (
              <K8CertContainer>
                <Typography variant="body2" sx={{ color: '#0B1117' }}>
                  {t('certTypeLabel')}
                </Typography>
                <YBRadioGroupField
                  name={COMMON_CERT_TYPE_FIELD}
                  control={control}
                  options={CERT_OPTIONS}
                  dataTestId="certType"
                  sx={{ gap: 2 }}
                />
                {certTypeVal === CertType.CUSTOM && (
                  <Controller
                    name={COMMON_CERT_FIELD}
                    control={control}
                    rules={{ required: 'This field is required' }}
                    render={({ field, fieldState }) => {
                      const value = certificates.find((i) => i.uuid === field.value) ?? '';
                      return (
                        <Box
                          sx={{ display: 'flex', flexDirection: 'column', width: '100%', pl: 4 }}
                          data-testid="RootCertificateField-Container"
                        >
                          <YBLabel>{t('selectRootCert')}</YBLabel>
                          <Box
                            sx={{
                              display: 'flex',
                              width: '100%'
                            }}
                          >
                            <YBAutoComplete
                              loading={isLoading}
                              options={(certificates as unknown) as Record<string, string>[]}
                              getOptionLabel={getOptionLabel}
                              fullWidth={true}
                              onChange={handleChange}
                              value={(value as unknown) as never}
                              ybInputProps={{
                                placeholder: t('rootCertificatePlaceHolder'),
                                error: !!fieldState.error,
                                helperText: fieldState.error?.message,
                                dataTestId: 'root-certificate-field'
                              }}
                              dataTestId="root-certificate-field-container"
                              size="large"
                            />
                          </Box>
                        </Box>
                      );
                    }}
                  />
                )}
              </K8CertContainer>
            )}
          </Box>
        </Box>
      )}
    </FieldContainer>
  );
};
