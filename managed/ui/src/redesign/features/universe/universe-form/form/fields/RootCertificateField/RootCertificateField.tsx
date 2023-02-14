import React, { FC, ChangeEvent } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBAutoComplete } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import { CloudType, UniverseFormData } from '../../../utils/dto';
import { PROVIDER_FIELD, ROOT_CERT_FIELD } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

const getOptionLabel = (option: Record<string, string>): string => option.label ?? '';
interface RootCertificateFieldProps {
  disabled: boolean;
  isPrimary: boolean;
  isCreateMode: boolean;
}

export const RootCertificateField: FC<RootCertificateFieldProps> = ({
  disabled,
  isPrimary,
  isCreateMode
}) => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD }); //provider data

  //fetch data
  const { data: certificates = [], isLoading } = useQuery(
    QUERY_KEY.getCertificates,
    api.getCertificates
  );

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(ROOT_CERT_FIELD, option?.uuid);
  };

  const renderOption = (option: Record<string, string>): React.ReactNode => {
    let isDisabled = false;

    if (option?.certType === 'CustomCertHostPath') {
      if (isPrimary && isCreateMode) {
        isDisabled = provider?.code !== CloudType.onprem;
      } else {
        isDisabled = provider?.code === CloudType.onprem;
      }
    }

    if (isDisabled)
      return (
        <Box
          onClick={(e) => {
            e.stopPropagation();
            e.preventDefault();
          }}
          className={classes.itemDisabled}
        >
          {option.label}
        </Box>
      );
    else return <Box>{option.label}</Box>;
  };

  return (
    <Controller
      name={ROOT_CERT_FIELD}
      control={control}
      render={({ field, fieldState }) => {
        const value = certificates.find((i) => i.uuid === field.value) ?? '';
        return (
          <Box display="flex" width="100%" data-testid="RootCertificateField-Container">
            <YBLabel dataTestId="RootCertificateField-Label">
              {t('universeForm.securityConfig.encryptionSettings.rootCertificate')}
            </YBLabel>
            <Box flex={1} className={classes.defaultTextBox}>
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
                  'data-testid': 'RootCertificateField-AutoComplete'
                }}
              />
            </Box>
          </Box>
        );
      }}
    />
  );
};
