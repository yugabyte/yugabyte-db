import { ChangeEvent, ReactElement, useState } from 'react';
import pluralize from 'pluralize';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import {
  YBLabel,
  YBAutoComplete,
  YBHelper,
  YBHelperVariants,
  YBToggleField
} from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import {
  sortAndGroup,
  getDefaultInstanceType,
  isEphemeralAwsStorageInstance,
  canUseSpotInstance
} from './InstanceTypeFieldHelper';
import { NodeType } from '../../../../../../utils/dtos';
import { IsOsPatchingEnabled } from '../../../../../../../components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';

import {
  AvailabilityZone,
  CloudType,
  InstanceType,
  InstanceTypeWithGroup,
  MasterPlacementMode,
  StorageType,
  UniverseFormData
} from '../../../utils/dto';
import {
  INSTANCE_TYPE_FIELD,
  PROVIDER_FIELD,
  DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_PLACEMENT_FIELD,
  SPOT_INSTANCE_FIELD,
  PLACEMENTS_FIELD,
  CPU_ARCHITECTURE_FIELD
} from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

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

interface InstanceTypeFieldProps {
  isEditMode?: boolean;
  isDedicatedMasterField?: boolean;
  disabled: boolean;
}

export const InstanceTypeField = ({
  isEditMode,
  isDedicatedMasterField,
  disabled
}: InstanceTypeFieldProps): ReactElement => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();
  const nodeTypeTag = isDedicatedMasterField ? NodeType.Master : NodeType.TServer;

  // To set value based on master or tserver field in dedicated mode
  const UPDATE_FIELD = isDedicatedMasterField ? MASTER_INSTANCE_TYPE_FIELD : INSTANCE_TYPE_FIELD;

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const cpuArch = useWatch({ name: CPU_ARCHITECTURE_FIELD });

  const deviceInfo = isDedicatedMasterField
    ? useWatch({ name: MASTER_DEVICE_INFO_FIELD })
    : useWatch({ name: DEVICE_INFO_FIELD });
  const masterPlacement = useWatch({ name: MASTER_PLACEMENT_FIELD });
  const zones = useWatch({ name: PLACEMENTS_FIELD }).map((zone: AvailabilityZone) => zone.name);

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(UPDATE_FIELD, option?.instanceTypeCode, { shouldValidate: true });
  };

  //fetch run time configs
  const {
    data: providerRuntimeConfigs,
    refetch: providerConfigsRefetch
  } = useQuery(QUERY_KEY.fetchProviderRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, provider?.uuid)
  );

  const isOsPatchingEnabled = IsOsPatchingEnabled();

  const { data, isLoading, refetch } = useQuery(
    [QUERY_KEY.getInstanceTypes, provider?.uuid, JSON.stringify(zones), isOsPatchingEnabled ? cpuArch : null],
    () => api.getInstanceTypes(provider?.uuid, zones, isOsPatchingEnabled ? cpuArch : null),
    {
      enabled: !!provider?.uuid && zones.length > 0,
      onSuccess: (data) => {
        if (!data.length) return;

        const currentInstanceType = getValues(UPDATE_FIELD);

        //do instance type exists on the last fetched list
        const instanceExists = (instanceType: string) =>
          !!data.find(
            (instance: InstanceType) => instance.instanceTypeCode === instanceType // default instance type exists in the list
          );

        // set default/first item as instance type after provider changes
        if (!currentInstanceType || !instanceExists(currentInstanceType)) {
          const defaultInstanceType = getDefaultInstanceType(provider.code, providerRuntimeConfigs);
          if (instanceExists(defaultInstanceType))
            setValue(UPDATE_FIELD, defaultInstanceType, { shouldValidate: true });
          else setValue(UPDATE_FIELD, data[0].instanceTypeCode, { shouldValidate: true });
        }
      }
    }
  );

  useUpdateEffect(() => {
    const getProviderRuntimeConfigs = async () => {
      await providerConfigsRefetch();
      //Reset instance type after provider change
      setValue(UPDATE_FIELD, null);
      //refetch instances based on changed provider
      refetch();
    };
    getProviderRuntimeConfigs();
  }, [provider?.uuid]);

  const instanceTypes = sortAndGroup(data, provider?.code);

  return (
    <Controller
      name={UPDATE_FIELD}
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
          <>
            <Box
              display="flex"
              width="100%"
              data-testid={`InstanceTypeField-${nodeTypeTag}-Container`}
              mt={2}
            >
              <YBLabel dataTestId={`InstanceTypeField-${nodeTypeTag}-Label`}>
                {t('universeForm.instanceConfig.instanceType')}
              </YBLabel>
              <Box
                flex={1}
                className={
                  masterPlacement === MasterPlacementMode.COLOCATED
                    ? classes.defaultTextBox
                    : classes.instanceConfigTextBox
                }
              >
                <YBAutoComplete
                  loading={isLoading}
                  disabled={disabled}
                  value={(value as unknown) as Record<string, string>}
                  options={(instanceTypes as unknown) as Record<string, string>[]}
                  getOptionLabel={getOptionLabel}
                  renderOption={renderOption}
                  onChange={handleChange}
                  ybInputProps={{
                    error: !!fieldState.error,
                    helperText: fieldState.error?.message,
                    'data-testid': `InstanceTypeField-${nodeTypeTag}-AutoComplete`
                  }}
                  groupBy={
                    [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code)
                      ? (option: Record<string, string>) => option.groupName
                      : undefined
                  }
                />

                {(isAWSEphemeralStorage || isGCPEphemeralStorage) && (
                  <YBHelper
                    dataTestId={`InstanceTypeField-${nodeTypeTag}-Helper`}
                    variant={YBHelperVariants.warning}
                  >
                    {t('universeForm.instanceConfig.ephemeralStorage')}
                  </YBHelper>
                )}
              </Box>
            </Box>
            {[CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code) &&
              canUseSpotInstance(providerRuntimeConfigs) && (
                <Box display="flex" width="100%" mt={2}>
                  <YBLabel dataTestId={`SpotInstanceField-${nodeTypeTag}-Label`}>
                    {t('universeForm.instanceConfig.useSpotInstance')}
                  </YBLabel>
                  <Box
                    flex={1}
                    className={
                      masterPlacement === MasterPlacementMode.COLOCATED
                        ? classes.defaultTextBox
                        : classes.instanceConfigTextBox
                    }
                  >
                    <YBToggleField
                      disabled={isEditMode}
                      name={SPOT_INSTANCE_FIELD}
                      control={control}
                    />
                  </Box>
                </Box>
              )}
          </>
        );
      }}
    />
  );
};
