import React, { ChangeEvent, FC } from 'react';
import pluralize from 'pluralize';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBAutoComplete, YBHelper, YBHelperVariants } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import {
  sortAndGroup,
  DEFAULT_INSTANCE_TYPES,
  isEphemeralAwsStorageInstance
} from './InstanceTypeFieldHelper';
import {
  CloudType,
  InstanceType,
  InstanceTypeWithGroup,
  StorageType,
  UniverseFormData
} from '../../../utils/dto';
import { INSTANCE_TYPE_FIELD, PROVIDER_FIELD, DEVICE_INFO_FIELD } from '../../../utils/constants';

const getOptionLabel = (op: Record<string, string>): string => {
  if (!op) return '';

  const option = (op as unknown) as InstanceType;
  let result = option.instanceTypeCode;
  if (option.numCores && option.memSizeGB) {
    const cores = pluralize('core', option.numCores, true);
    result = `${option.instanceTypeCode} (${cores}, ${option.memSizeGB}GB RAM)`;
  }
  return result;
};

const renderOption = (option: Record<string, string>) => {
  return <>{getOptionLabel(option)}</>;
};

export const InstanceTypeField: FC = () => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const deviceInfo = useWatch({ name: DEVICE_INFO_FIELD });

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(INSTANCE_TYPE_FIELD, option?.instanceTypeCode, { shouldValidate: true });
  };

  const { data, isLoading, refetch } = useQuery(
    [QUERY_KEY.getInstanceTypes, provider?.uuid],
    () => api.getInstanceTypes(provider?.uuid),
    {
      enabled: !!provider?.uuid,
      onSuccess: (data) => {
        // set default/first item as instance type after provider changes
        if (!getValues(INSTANCE_TYPE_FIELD) && provider?.code && data.length) {
          const defaultInstanceType =
            DEFAULT_INSTANCE_TYPES[provider.code] || data[0].instanceTypeCode;
          setValue(INSTANCE_TYPE_FIELD, defaultInstanceType, { shouldValidate: true });
        }
      }
    }
  );

  useUpdateEffect(() => {
    //Reset instance type after provider change
    setValue(INSTANCE_TYPE_FIELD, null);
    //refetch instances based on changed provider
    refetch();
  }, [provider]);

  const instanceTypes = sortAndGroup(data, provider?.code);

  return (
    <Controller
      name={INSTANCE_TYPE_FIELD}
      control={control}
      rules={{
        required: t('universeForm.validation.required', {
          field: t('universeForm.instanceConfig.instanceType')
        }) as string
      }}
      render={({ field, fieldState }) => {
        const value =
          instanceTypes.find((i: InstanceTypeWithGroup) => i.instanceTypeCode === field.value) ??
          '';

        //is ephemeral storage
        const isAWSEphemeralStorage =
          value && provider.code === CloudType.aws && isEphemeralAwsStorageInstance(value);
        const isGCPEphemeralStorage =
          value &&
          provider.code === CloudType.gcp &&
          deviceInfo?.storageType === StorageType.Scratch;

        return (
          <Box display="flex" width="100%" data-testid="InstanceTypeField-Container">
            <YBLabel dataTestId="InstanceTypeField-Label">
              {t('universeForm.instanceConfig.instanceType')}
            </YBLabel>
            <Box flex={1}>
              <YBAutoComplete
                loading={isLoading}
                value={(value as unknown) as Record<string, string>}
                options={(instanceTypes as unknown) as Record<string, string>[]}
                getOptionLabel={getOptionLabel}
                renderOption={renderOption}
                onChange={handleChange}
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  'data-testid': 'InstanceTypeField-AutoComplete'
                }}
                groupBy={
                  [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code)
                    ? (option: Record<string, string>) => option.groupName
                    : undefined
                }
              />

              {(isAWSEphemeralStorage || isGCPEphemeralStorage) && (
                <YBHelper dataTestId="InstanceTypeField-Helper" variant={YBHelperVariants.warning}>
                  {t('universeForm.instanceConfig.ephemeralStorage')}
                </YBHelper>
              )}
            </Box>
          </Box>
        );
      }}
    />
  );
};
