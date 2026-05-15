import { ChangeEvent, ReactElement, useLayoutEffect } from 'react';
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
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import { getDeviceInfoFromInstance } from '@app/redesign/features-v2/universe/create-universe/fields/volume-info/VolumeInfoFieldHelper';
import { NodeType } from '@app/redesign/utils/dtos';
import {
  CloudType,
  InstanceType,
  InstanceTypeWithGroup,
  Placement,
  Region
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
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

  const option = op as unknown as InstanceType;
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
  provider?: Partial<ProviderType>;
  regions?: Region[];
}

export const InstanceTypeField = ({
  isEditMode = false,
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
      enabled: !!provider?.uuid && zoneNames.length > 0 && !isLoadingZones
    }
  );

  useLayoutEffect(() => {
    if (!data?.length || !provider?.code) return;

    const instanceExists = (code: string | null | undefined) =>
      !!code && !!data.find((instance) => instance.instanceTypeCode === code);

    const currentInstanceType = getValues(UPDATE_FIELD);

    if (!instanceExists(currentInstanceType)) {
      const defaultInstanceType = getDefaultInstanceType(provider.code, providerRuntimeConfigs);
      const code =
        defaultInstanceType && instanceExists(defaultInstanceType)
          ? defaultInstanceType
          : data[0].instanceTypeCode;
      setValue(UPDATE_FIELD, code, { shouldValidate: true });
      const option = data.find((i) => i.instanceTypeCode === code);
      if (option) {
        setValue(
          UPDATE_DEVICE_INFO_FIELD,
          getDeviceInfoFromInstance(option, providerRuntimeConfigs),
          {
            shouldValidate: true
          }
        );
      }
    } else if (!isEditMode && !getValues(UPDATE_DEVICE_INFO_FIELD)) {
      const option = data.find((i) => i.instanceTypeCode === currentInstanceType);
      if (option) {
        setValue(
          UPDATE_DEVICE_INFO_FIELD,
          getDeviceInfoFromInstance(option, providerRuntimeConfigs),
          {
            shouldValidate: true
          }
        );
      }
    }
  }, [
    data,
    provider?.code,
    providerRuntimeConfigs,
    UPDATE_FIELD,
    UPDATE_DEVICE_INFO_FIELD,
    getValues,
    setValue,
    isEditMode
  ]);

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
                size="large"
                loading={isLoading}
                disabled={disabled}
                value={value as unknown as Record<string, string>}
                options={instanceTypes as unknown as Record<string, string>[]}
                getOptionLabel={getOptionLabel}
                renderOption={renderOption}
                onChange={handleChange}
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  label: t('createUniverseV2.instanceSettings.instanceType'),
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
