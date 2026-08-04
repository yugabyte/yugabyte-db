import { FC, ChangeEvent } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBAutoComplete } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import { DEFAULT_INSTANCE_CONFIG, KmsConfig, UniverseFormData } from '../../../utils/dto';
import { EAR_FIELD, EBS_KMS_CONFIG_FIELD } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';
import { ProviderLabel } from '@app/components/configRedesign/providerRedesign/constants';

const renderOption = (op: Record<string, string>): string => {
  const option = (op as unknown) as KmsConfig;
  return option.metadata.name;
};

const getOptionLabel = (op: Record<string, string>): string => {
  const option = (op as unknown) as KmsConfig;
  return option?.metadata?.name ?? '';
};

interface EBSKmsConfigFieldProps {
  disabled?: boolean;
}

export const EBSKmsConfigField: FC<EBSKmsConfigFieldProps> = ({ disabled }) => {
  const { setValue, control } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();

  //fetch data
  const { data, isLoading } = useQuery(QUERY_KEY.getKMSConfigs, api.getKMSConfigs);
  let kmsConfigs: KmsConfig[] = data ? _.sortBy(data, 'metadata.provider', 'metadata.name') : [];
  kmsConfigs = kmsConfigs.filter((kms) => kms.metadata.provider === ProviderLabel.aws);

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(
      EBS_KMS_CONFIG_FIELD,
      option?.metadata?.configUUID ?? DEFAULT_INSTANCE_CONFIG.kmsConfig,
      {
        shouldValidate: true
      }
    );
  };

  return (
    <Controller
      name={EBS_KMS_CONFIG_FIELD}
      control={control}
      rules={{
        required: !disabled
          ? (t('universeForm.validation.required', {
              field: t('universeForm.instanceConfig.kmsConfig')
            }) as string)
          : ''
      }}
      render={({ field, fieldState }) => {
        const value = kmsConfigs.find((i) => i.metadata.configUUID === field.value) ?? '';
        return (
          <Box display="flex" width="100%" data-testid="EBSKmsConfig-Container">
            <YBLabel dataTestId="EBSKmsConfig-Label">
              {t('universeForm.instanceConfig.kmsConfig')}
            </YBLabel>
            <Box flex={1} className={classes.defaultTextBox}>
              <YBAutoComplete
                disabled={disabled}
                loading={isLoading}
                options={(kmsConfigs as unknown) as Record<string, string>[]}
                ybInputProps={{
                  placeholder: t('universeForm.instanceConfig.kmsConfigPlaceHolder'),
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  InputProps: { autoFocus: true },
                  'data-testid': 'EBSKmsConfig-AutoComplete'
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
