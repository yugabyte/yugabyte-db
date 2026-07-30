import { FC, ChangeEvent } from 'react';
import { sortBy } from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { YBAutoComplete } from '@yugabyte-ui-library/core';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import { KmsConfig } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderLabel } from '@app/components/configRedesign/providerRedesign/constants';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import { EBS_KMS_CONFIG_FIELD } from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

const getOptionLabel = (op: string | Record<string, string>): string => {
  const option = (op as unknown) as KmsConfig;
  return option?.metadata?.name ?? '';
};

interface EBSKmsConfigFieldProps {
  disabled?: boolean;
}

export const EBSKmsConfigField: FC<EBSKmsConfigFieldProps> = ({ disabled }) => {
  const { setValue, control } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation');

  const { data, isLoading } = useQuery(QUERY_KEY.getKMSConfigs, api.getKMSConfigs);
  let kmsConfigs: KmsConfig[] = data ? sortBy(data, 'metadata.provider', 'metadata.name') : [];
  kmsConfigs = kmsConfigs.filter((kms) => kms.metadata.provider === ProviderLabel.aws);

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(EBS_KMS_CONFIG_FIELD, option?.metadata?.configUUID ?? null, {
      shouldValidate: true
    });
  };

  return (
    <Controller
      name={EBS_KMS_CONFIG_FIELD}
      control={control}
      render={({ field, fieldState }) => {
        const value = kmsConfigs.find((i) => i.metadata.configUUID === field.value) ?? '';
        return (
          <YBAutoComplete
            disabled={disabled}
            loading={isLoading}
            options={(kmsConfigs as unknown) as Record<string, string>[]}
            ybInputProps={{
              placeholder: t('createUniverseV2.instanceSettings.kmsConfigPlaceHolder'),
              error: !!fieldState.error,
              helperText: fieldState.error?.message,
              dataTestId: 'EBSKmsConfig-AutoComplete',
              label: t('createUniverseV2.instanceSettings.kmsConfig')
            }}
            getOptionLabel={getOptionLabel}
            onChange={handleChange}
            value={(value as unknown) as never}
            dataTestId="ebs-kms-config-autocomplete"
            size="large"
          />
        );
      }}
    />
  );
};
