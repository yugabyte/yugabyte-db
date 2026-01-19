import { ChangeEvent, ReactElement } from 'react';
import pluralize from 'pluralize';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { YBAutoComplete, mui } from '@yugabyte-ui-library/core';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import {
  sortAndGroup,
  getDefaultInstanceType,
  useGetZones
} from '@app/redesign/features-v2/universe/create-universe/fields/instance-type/InstanceTypeFieldHelper';
import { NodeType } from '@app/redesign/utils/dtos';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import {
  CloudType,
  InstanceType,
  InstanceTypeWithGroup,
  Placement,
  Region
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import { getDeviceInfoFromInstance } from '@app/redesign/features-v2/universe/create-universe/fields/volume-info/VolumeInfoFieldHelper';
import {
  INSTANCE_TYPE_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  CPU_ARCH_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  DEVICE_INFO_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

const { Box } = mui;

const getOptionLabel = (op: Record<string, string> | string): string => {
  if (!op) return '';

  const option = (op as unknown) as InstanceType;
  let result = option.instanceTypeCode;
  if (option.numCores && option.memSizeGB) {
    const cores = pluralize('core', option.numCores, true);
    result = `${option.instanceTypeCode} (${cores}, ${option.memSizeGB}GB RAM)`;
  }
  return result;
};

const renderOption = (
  props: React.HTMLAttributes<HTMLLIElement>,
  option: Record<string, string>
): React.ReactNode => {
  return <li {...props}>{getOptionLabel(option)}</li>;
};

interface InstanceTypeFieldProps {
  isEditMode?: boolean;
  isMaster?: boolean;
  disabled: boolean;
  provider?: ProviderType;
  regions?: Region[];
}

export const InstanceTypeField = ({
  isMaster,
  disabled,
  provider,
  regions
}: InstanceTypeFieldProps): ReactElement => {
  const { control, setValue, getValues, watch } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation();
  const nodeTypeTag = isMaster ? NodeType.Master : NodeType.TServer;

  // To set value based on master or tserver field in dedicated mode
  const UPDATE_FIELD = isMaster ? MASTER_INSTANCE_TYPE_FIELD : INSTANCE_TYPE_FIELD;
  const UPDATE_DEVICE_INFO_FIELD = isMaster ? MASTER_DEVICE_INFO_FIELD : DEVICE_INFO_FIELD;

  const cpuArch = watch(CPU_ARCH_FIELD);
  const { zones, isLoadingZones } = useGetZones(provider, regions);
  const zoneNames = zones.map((zone: Placement) => zone.name);

  //fetch run time configs
  const { providerRuntimeConfigs, osPatchingEnabled } = useRuntimeConfigValues(provider?.uuid);

  const { data, isLoading } = useQuery(
    [
      QUERY_KEY.getInstanceTypes,
      provider?.uuid,
      JSON.stringify(zoneNames),
      osPatchingEnabled ? cpuArch : null
    ],
    () => api.getInstanceTypes(provider?.uuid, zoneNames, osPatchingEnabled ? cpuArch : null),
    {
      enabled: !!provider?.uuid && zoneNames.length > 0 && !isLoadingZones,
      onSuccess: (data) => {
        if (!data.length) return;

        const currentInstanceType = getValues(UPDATE_FIELD);

        //do instance type exists on the last fetched list
        const instanceExists = (instanceType: string) =>
          !!data.find(
            (instance: InstanceType) => instance.instanceTypeCode === instanceType // default instance type exists in the list
          );

        // set default/first item as instance type after provider changes
        if (provider && (!currentInstanceType || !instanceExists(currentInstanceType))) {
          const defaultInstanceType = getDefaultInstanceType(provider.code, providerRuntimeConfigs);
          if (instanceExists(defaultInstanceType))
            setValue(UPDATE_FIELD, defaultInstanceType, { shouldValidate: true });
          else setValue(UPDATE_FIELD, data[0].instanceTypeCode, { shouldValidate: true });
        }
      }
    }
  );
  const instanceTypes = sortAndGroup(data, provider?.code);

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(UPDATE_FIELD, option?.instanceTypeCode, { shouldValidate: true });
    const deviceInfo = getDeviceInfoFromInstance(option, providerRuntimeConfigs);
    setValue(UPDATE_DEVICE_INFO_FIELD, deviceInfo, { shouldValidate: true });
  };

  return (
    <Controller
      name={UPDATE_FIELD}
      control={control}
      render={({ field, fieldState }) => {
        const value =
          instanceTypes.find((i: InstanceTypeWithGroup) => i.instanceTypeCode === field.value) ??
          '';
        return (
          <Box
            display="flex"
            width="100%"
            data-testid={`InstanceTypeField-${nodeTypeTag}-Container`}
          >
            <Box flex={1}>
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
                  label: t('universeForm.instanceConfig.instanceType'),
                  dataTestId: 'instance-type-field'
                }}
                dataTestId="instance-type-field-container"
                groupBy={
                  provider?.code &&
                  [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code)
                    ? (option: Record<string, string>) => option.groupName
                    : undefined
                }
              />
            </Box>
          </Box>
        );
      }}
    />
  );
};
