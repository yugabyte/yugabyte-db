import { FC, ChangeEvent } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBAutoComplete } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import { DEFAULT_INSTANCE_CONFIG, KmsConfig, UniverseFormData } from '../../../utils/dto';
import { KMS_CONFIG_FIELD, EAR_FIELD } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

const renderOption = (op: Record<string, string>): string => {
  const option = (op as unknown) as KmsConfig;
  return option.metadata.name;
};

const getOptionLabel = (op: Record<string, string>): string => {
  const option = (op as unknown) as KmsConfig;
  return option?.metadata?.name ?? '';
};

interface KMSConfigFieldProps {
  disabled?: boolean;
}

export const KMSConfigField: FC<KMSConfigFieldProps> = ({ disabled }) => {
  const { setValue, control } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();

  //watchers
  const encryptionEnabled = useWatch({ name: EAR_FIELD });

  //fetch data
  const { data, isLoading } = useQuery(QUERY_KEY.getKMSConfigs, api.getKMSConfigs);
  let kmsConfigs: KmsConfig[] = data ? _.sortBy(data, 'metadata.provider', 'metadata.name') : [];

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(KMS_CONFIG_FIELD, option?.metadata?.configUUID ?? DEFAULT_INSTANCE_CONFIG.kmsConfig, {
      shouldValidate: true
    });
  };

  return (
    <Controller
      name={KMS_CONFIG_FIELD}
      control={control}
      rules={{
        required:
          !disabled && encryptionEnabled
            ? (t('universeForm.validation.required', {
                field: t('universeForm.instanceConfig.kmsConfig')
              }) as string)
            : ''
      }}
      render={({ field, fieldState }) => {
        const value = kmsConfigs.find((i) => i.metadata.configUUID === field.value) ?? '';
        return (
          <Box display="flex" width="100%" data-testid="KMSConfigField-Container">
            <YBLabel dataTestId="KMSConfigField-Label">
              {t('universeForm.instanceConfig.kmsConfig')}
            </YBLabel>
            <Box flex={1} className={classes.defaultTextBox}>
              <YBAutoComplete
                disabled={disabled}
                loading={isLoading}
                options={(kmsConfigs as unknown) as Record<string, string>[]}
                groupBy={(option: Record<string, any>) => option?.metadata?.provider} //group by provider
                ybInputProps={{
                  placeholder: t('universeForm.instanceConfig.kmsConfigPlaceHolder'),
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  InputProps: { autoFocus: true },
                  'data-testid': 'KMSConfigField-AutoComplete'
                }}
                ref={field.ref}
                getOptionLabel={getOptionLabel}
                renderOption={renderOption}
                onChange={handleChange}
                value={(value as unknown) as never}
              />
            </Box>
          </Box>
        );
      }}
    />
  );
};

//TODO - Filter HC Vault data based on feature flag -> HCVault
//show only if Encryption at rest enabled
