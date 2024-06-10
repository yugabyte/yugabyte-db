import React, { FC, ChangeEvent } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { Box, Chip } from '@material-ui/core';
import { YBAutoComplete, YBTooltip } from '../../../../../components';
import { api, QUERY_KEY } from '../../../../../utils/api';
import {
  EncryptionInTransitFormValues,
  useEITStyles,
  CertTypes
} from '../EncryptionInTransitUtils';
import { Certificate } from '../../../../../helpers/dtos';

const getOptionLabel = (option: Record<string, string>): string => option.label ?? '';

interface CertificateFieldProps {
  disabled: boolean;
  name: CertTypes;
  label: string;
  activeCert: string;
  tooltipMsg?: string;
}

export const CertificateField: FC<CertificateFieldProps> = ({
  disabled,
  name,
  activeCert,
  label,
  tooltipMsg
}) => {
  const { control, setValue } = useFormContext<EncryptionInTransitFormValues>();
  const { t } = useTranslation();
  const classes = useEITStyles();

  //fetch data
  const { data: certificates = [], isLoading } = useQuery(
    QUERY_KEY.getCertificates,
    api.getCertificates
  );

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(name, option?.uuid ?? null, { shouldValidate: true });
  };

  const renderOption = (op: Record<string, string>): React.ReactElement => {
    const option = (op as unknown) as Certificate;
    const isActive = activeCert && activeCert === option?.uuid;
    return (
      <>
        {' '}
        {option.label}
        {isActive && <Chip size="small" className={classes.chip} label={t('common.active')} />}
      </>
    );
  };

  return (
    <Controller
      name={name}
      control={control}
      render={({ field, fieldState }) => {
        const value = certificates.find((i: Certificate) => i.uuid === field.value) ?? '';
        return (
          <Box
            display="flex"
            flexDirection="column"
            width="100%"
            data-testid="RootCertificateField-Container"
          >
            <Box flex={1}>
              <YBTooltip title={tooltipMsg ?? ''} placement="top">
                <span>
                  <YBAutoComplete
                    disabled={disabled}
                    loading={isLoading}
                    options={(certificates as unknown) as Record<string, string>[]}
                    getOptionLabel={getOptionLabel}
                    renderOption={renderOption}
                    onChange={handleChange}
                    value={(value as unknown) as never}
                    ybInputProps={{
                      placeholder: t(
                        'universeForm.securityConfig.encryptionSettings.rootCertificatePlaceHolder'
                      ),
                      error: !!fieldState.error,
                      helperText: fieldState.error?.message,
                      'data-testid': `${name}Field-AutoComplete`,
                      label
                    }}
                    classes={{
                      root: classes.inputField
                    }}
                  />
                </span>
              </YBTooltip>
            </Box>
          </Box>
        );
      }}
    />
  );
};
