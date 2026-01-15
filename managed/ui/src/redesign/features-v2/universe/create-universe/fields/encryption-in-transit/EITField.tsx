import { FC } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch, Controller, FieldPath } from 'react-hook-form';
import {
  mui,
  YBToggleField,
  YBLabel,
  YBAutoComplete,
  YBCheckboxField
} from '@yugabyte-ui-library/core';
import { FieldContainer } from '../../components/DefaultComponents';
import { QUERY_KEY, api } from '../../../../../features/universe/universe-form/utils/api';

import { SecuritySettingsProps } from '../../steps/security-settings/dtos';
import NextLineIcon from '../../../../../assets/next-line.svg';
import { useUpdateEffect } from 'react-use';

const { Box, Typography } = mui;

interface EARProps {
  disabled: boolean;
}

interface CertCompProps {
  toggleFieldPath: FieldPath<SecuritySettingsProps>;
  certFieldPath: FieldPath<SecuritySettingsProps>;
  generateCertFieldPath: FieldPath<SecuritySettingsProps>;
}

const getOptionLabel = (option: any): string => option.label ?? '';

//
const USE_SAME_CERT_FIELD = 'useSameCertificate';
const ENABLE_BOTH_FIELD = 'enableBothEncryption';
const COMMON_CERT_FIELD = 'rootCertificate';
const COMMON_GEN_CERT_FIELD = 'generateCerticate';

const CERTComponent: FC<CertCompProps> = ({
  toggleFieldPath,
  certFieldPath,
  generateCertFieldPath
}) => {
  const { control, setValue } = useFormContext<SecuritySettingsProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.securitySettings.eitField'
  });

  const isOptionEnabled = useWatch({ name: toggleFieldPath });
  const isSelfGenerated = useWatch({ name: generateCertFieldPath });

  useUpdateEffect(() => {
    setValue(certFieldPath, '');
  }, [isSelfGenerated]);

  //fetch data
  const { data: certificates = [], isLoading } = useQuery(
    QUERY_KEY.getCertificates,
    api.getCertificates
  );

  const handleChange = (e: any, option: any) => {
    setValue(certFieldPath, option?.uuid);
  };

  return (
    <FieldContainer sx={{ padding: '16px 24px' }}>
      <YBToggleField
        control={control}
        name={toggleFieldPath}
        label={t(toggleFieldPath)}
        dataTestId={`enable-encryption-in-transit-field`}
      />
      {isOptionEnabled && (
        <Box sx={{ display: 'flex', flexDirection: 'row', width: '100%', mt: 4, gap: '16px' }}>
          <Box sx={{ mt: 4 }}>
            <NextLineIcon />
          </Box>
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: '8px', width: '100%' }}>
            <Controller
              name={certFieldPath}
              control={control}
              render={({ field, fieldState }) => {
                const value = certificates.find((i) => i.uuid === field.value) ?? '';
                return (
                  <Box
                    sx={{ display: 'flex', flexDirection: 'column', width: '100%' }}
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
                        disabled={isSelfGenerated}
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
                      />
                    </Box>
                  </Box>
                );
              }}
            />

            <Typography variant="body2" sx={{ color: '#6D7C88' }}>
              or
            </Typography>
            <YBCheckboxField
              size="large"
              name={generateCertFieldPath}
              control={control}
              label={t('genSelfSignedCert')}
              dataTestId={`generate-self-signed-cert-field`}
            />
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

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', gap: '16px' }}>
      <YBCheckboxField
        control={control}
        name={USE_SAME_CERT_FIELD}
        label={t('useSameCert')}
        size="large"
        dataTestId={`use-same-certificate-field`}
      ></YBCheckboxField>
      {useSameCertValue ? (
        <CERTComponent
          toggleFieldPath={ENABLE_BOTH_FIELD}
          certFieldPath={COMMON_CERT_FIELD}
          generateCertFieldPath={COMMON_GEN_CERT_FIELD}
        />
      ) : (
        <>
          <CERTComponent
            toggleFieldPath={'enableNodeToNodeEncryption'}
            certFieldPath={'rootNToNCertificate'}
            generateCertFieldPath={'generateNToNCertiacte'}
          />
          <CERTComponent
            toggleFieldPath={'enableClientToNodeEncryption'}
            certFieldPath={'rootCToNCertificate'}
            generateCertFieldPath={'generateCToNCertificate'}
          />
        </>
      )}
    </Box>
  );
};
