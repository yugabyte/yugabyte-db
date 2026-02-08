import { FC, useEffect } from 'react';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { YBInput, YBLabel, YBSelect, mui } from '@yugabyte-ui-library/core';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import {
  getDeviceInfoFromInstance,
  getIopsByStorageType,
  getMaxDiskIops,
  getMinDiskIops,
  getStorageTypeOptions,
  getThroughputByIops,
  getThroughputByStorageType,
  useVolumeControls
} from '@app/redesign/features-v2/universe/create-universe/fields/volume-info/VolumeInfoFieldHelper';
import {
  isEphemeralAwsStorageInstance,
  useGetZones
} from '@app/redesign/features-v2/universe/create-universe/fields/instance-type/InstanceTypeFieldHelper';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import {
  CloudType,
  Placement,
  StorageType,
  VolumeType
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import {
  CPU_ARCHITECTURE_FIELD,
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

//icons
import Close from '@app/redesign/assets/close.svg';

const { Box, MenuItem } = mui;

interface VolumeInfoFieldProps {
  isMaster?: boolean;
  maxVolumeCount: number;
  disabled: boolean;
  provider?: Partial<ProviderType>;
  useDedicatedNodes?: boolean;
  regions?: Region[];
}

const menuProps = {
  anchorOrigin: {
    vertical: 'bottom',
    horizontal: 'left'
  },
  transformOrigin: {
    vertical: 'top',
    horizontal: 'left'
  }
} as any;

export const VolumeInfoField: FC<VolumeInfoFieldProps> = ({
  isMaster,
  maxVolumeCount,
  disabled,
  provider,
  useDedicatedNodes,
  regions
}) => {
  const { t } = useTranslation();
  const dataTag = isMaster ? 'Master' : 'TServer';

  // watchers
  const { watch, control, setValue } = useFormContext<InstanceSettingProps>();
  const fieldValue = isMaster ? watch(MASTER_DEVICE_INFO_FIELD) : watch(DEVICE_INFO_FIELD);
  const instanceType = isMaster ? watch(MASTER_INSTANCE_TYPE_FIELD) : watch(INSTANCE_TYPE_FIELD);
  const cpuArch = watch(CPU_ARCHITECTURE_FIELD);

  const {
    numVolumesDisable,
    volumeSizeDisable,
    minVolumeSize,
    disableIops,
    disableThroughput,
    disableStorageType
  } = useVolumeControls();

  const { zones, isLoadingZones } = useGetZones(provider, regions);
  const zoneNames = zones.map((zone: Placement) => zone.name);

  //fetch run time configs
  const { providerRuntimeConfigs, osPatchingEnabled } = useRuntimeConfigValues(provider?.uuid);

  // Update field is based on master or tserver field in dedicated mode
  const UPDATE_FIELD = isMaster ? MASTER_DEVICE_INFO_FIELD : DEVICE_INFO_FIELD;

  //get instance details
  const { data: instanceTypes } = useQuery(
    [
      QUERY_KEY.getInstanceTypes,
      provider?.uuid,
      JSON.stringify(zoneNames),
      osPatchingEnabled ? cpuArch : null
    ],
    () => api.getInstanceTypes(provider?.uuid, zoneNames, osPatchingEnabled ? cpuArch : null),
    { enabled: !!provider?.uuid && zoneNames.length > 0 && !isLoadingZones }
  );
  const instance = instanceTypes?.find((item) => item.instanceTypeCode === instanceType);

  // Update volume info after instance changes
  useEffect(() => {
    if (!instance || !provider?.uuid) return;
    const updateDeviceInfo = () => {
      const deviceInfo = getDeviceInfoFromInstance(instance, providerRuntimeConfigs);
      deviceInfo && setValue(UPDATE_FIELD, deviceInfo);
    };
    !fieldValue && updateDeviceInfo();
  }, [instance, provider?.uuid]);

  const convertToString = (str: string | number) => str?.toString() ?? '';

  //reset methods
  const resetThroughput = () => {
    if (!fieldValue) return;
    const { storageType, throughput, diskIops, volumeSize } = fieldValue;
    if (
      storageType &&
      diskIops &&
      [
        StorageType.IO1,
        StorageType.IO2,
        StorageType.GP3,
        StorageType.UltraSSD_LRS,
        StorageType.PremiumV2_LRS
      ].includes(storageType)
    ) {
      //resetting throughput
      const throughputVal = getThroughputByIops(Number(throughput), diskIops, storageType);
      setValue(UPDATE_FIELD, {
        ...fieldValue,
        throughput: throughputVal,
        volumeSize: volumeSize < minVolumeSize ? minVolumeSize : volumeSize
      });
    } else
      setValue(UPDATE_FIELD, {
        ...fieldValue,
        volumeSize: volumeSize < minVolumeSize ? minVolumeSize : volumeSize
      });
  };

  //field actions
  const onStorageTypeChanged = (storageType: StorageType) => {
    if (!fieldValue) return;
    const throughput = getThroughputByStorageType(storageType);
    const diskIops = getIopsByStorageType(storageType);
    setValue(UPDATE_FIELD, { ...fieldValue, throughput, diskIops, storageType });
  };

  const onVolumeSizeChanged = (value: any) => {
    if (!fieldValue) return;
    setValue(UPDATE_FIELD, {
      ...fieldValue,
      volumeSize: Number(value)
    });
  };

  /*
    When storage type is UltraSSD_LRS, disk IOPS is calculated based on volume size.
    Hence, when volume size is changed, disk IOPS should be recalculated.
  */
  useUpdateEffect(() => {
    if (
      fieldValue?.storageType === StorageType.UltraSSD_LRS ||
      fieldValue?.storageType === StorageType.PremiumV2_LRS
    ) {
      onDiskIopsChanged(fieldValue?.diskIops);
    }
  }, [fieldValue?.volumeSize]);

  const onDiskIopsChanged = (value: any) => {
    if (!fieldValue) return;
    const { storageType, volumeSize } = fieldValue;
    if (!storageType) return;
    const maxDiskIops = getMaxDiskIops(storageType, volumeSize);
    const minDiskIops = getMinDiskIops(storageType, volumeSize);
    const diskIops = Math.max(minDiskIops, Math.min(maxDiskIops, Number(value)));
    setValue(UPDATE_FIELD, { ...fieldValue, diskIops });
  };

  const onThroughputChange = (value: any) => {
    if (!fieldValue) return;
    const { storageType, diskIops } = fieldValue;
    if (!diskIops || !storageType) return;
    const throughput = getThroughputByIops(Number(value), diskIops, storageType);
    setValue(UPDATE_FIELD, { ...fieldValue, throughput });
  };

  const onNumVolumesChanged = (numVolumes: any) => {
    if (!fieldValue) return;
    const volumeCount = Number(numVolumes) > maxVolumeCount ? maxVolumeCount : Number(numVolumes);
    setValue(UPDATE_FIELD, { ...fieldValue, numVolumes: volumeCount });
  };

  //render
  if (!instance) return null;

  const { volumeDetailsList } = instance?.instanceTypeDetails;
  const { volumeType } = volumeDetailsList[0];

  if (![VolumeType.EBS, VolumeType.SSD, VolumeType.NVME].includes(volumeType)) return null;

  const renderVolumeInfo = () => {
    // Checking if provider code is OnPrem as it is provisioned to fixed size
    // and cannot be changed on both edit and create mode
    const fixedVolumeSize =
      (fieldValue?.storageType === StorageType.Scratch && provider?.code === CloudType.gcp) ||
      provider?.code === CloudType.onprem;

    const fixedNumVolumes =
      [VolumeType.SSD, VolumeType.NVME].includes(volumeType) &&
      provider?.code &&
      ![CloudType.kubernetes, CloudType.gcp, CloudType.azu].includes(provider?.code);

    // Ephemeral instances volume information cannot be resized, refer to PLAT-16118
    const isEphemeralStorage =
      provider?.code === CloudType.aws && isEphemeralAwsStorageInstance(instance);

    return (
      <Box display="flex" flexDirection="column">
        <Box display="flex">
          <Box>
            <YBLabel>{t('createUniverseV2.instanceSettings.volumeInfoPerNode')}</YBLabel>
          </Box>
        </Box>
        <Box sx={{ gap: '16px', display: 'flex' }}>
          <Box flex={1} sx={{ width: 198 }}>
            <YBInput
              type="number"
              fullWidth
              disabled={fixedNumVolumes || numVolumesDisable || isEphemeralStorage || disabled}
              slotProps={{
                htmlInput: {
                  min: 1,
                  'data-testid': `VolumeInfoField-${dataTag}-VolumeInput`,
                  disabled: fixedNumVolumes || numVolumesDisable || isEphemeralStorage || disabled
                }
              }}
              value={convertToString(fieldValue?.numVolumes ?? '')}
              onChange={(event) => onNumVolumesChanged(event.target.value)}
              inputMode="numeric"
              dataTestId={`VolumeInfoField-${dataTag}-VolumeInput`}
            />
          </Box>

          <Box display="flex" alignItems="center" justifyContent="center">
            <Close />
          </Box>

          <Box display="flex" alignItems="flex-end" flex={1} sx={{ width: 198 }}>
            <YBInput
              type="number"
              fullWidth
              disabled={isEphemeralStorage || fixedVolumeSize || volumeSizeDisable || disabled}
              slotProps={{
                htmlInput: {
                  min: 1,
                  'data-testid': `VolumeInfoField-${dataTag}-VolumeSizeInput`,
                  disabled: isEphemeralStorage || fixedVolumeSize || volumeSizeDisable || disabled
                }
              }}
              value={convertToString(fieldValue?.volumeSize ?? '')}
              onChange={(event) => onVolumeSizeChanged(event.target.value)}
              onBlur={resetThroughput}
              inputMode="numeric"
              dataTestId={`VolumeInfoField-${dataTag}-VolumeSizeInput`}
            />
          </Box>

          <Box
            display="flex"
            alignItems="center"
            sx={(theme) => ({
              alignSelf: 'flex-end',
              marginBottom: 1
            })}
          >
            {provider?.code === CloudType.kubernetes
              ? t('createUniverseV2.instanceSettings.k8VolumeSizeUnit')
              : t('createUniverseV2.instanceSettings.volumeSizeUnit')}
          </Box>
        </Box>
      </Box>
    );
  };

  const renderStorageType = () => {
    if (
      (provider?.code && [CloudType.gcp, CloudType.azu].includes(provider?.code)) ||
      (volumeType === VolumeType.EBS && provider?.code === CloudType.aws)
    ) {
      return (
        <Box display="flex" sx={{ width: 198 }} mt={2}>
          <YBSelect
            fullWidth
            label={
              provider?.code === CloudType.aws
                ? t('createUniverseV2.instanceSettings.ebs')
                : t('createUniverseV2.instanceSettings.ssd')
            }
            disabled={disableStorageType || disabled}
            value={fieldValue?.storageType}
            slotProps={{
              htmlInput: {
                min: 1,
                'data-testid': `VolumeInfoField-${dataTag}-StorageTypeSelect`,
                disabled
              }
            }}
            onChange={(event) =>
              onStorageTypeChanged((event?.target.value as unknown) as StorageType)
            }
            dataTestId={`VolumeInfoField-${dataTag}-StorageTypeSelect`}
            menuProps={menuProps}
          >
            {getStorageTypeOptions(provider?.code, providerRuntimeConfigs).map((item) => (
              <MenuItem key={item.value} value={item.value}>
                {item.label}
              </MenuItem>
            ))}
          </YBSelect>
        </Box>
      );
    }

    return null;
  };

  const renderDiskIops = () => {
    if (
      fieldValue?.storageType &&
      ![
        StorageType.IO1,
        StorageType.IO2,
        StorageType.GP3,
        StorageType.UltraSSD_LRS,
        StorageType.PremiumV2_LRS,
        StorageType.Hyperdisk_Balanced,
        StorageType.Hyperdisk_Extreme
      ].includes(fieldValue?.storageType)
    ) {
      return null;
    }

    return (
      <Box display="flex" sx={{ width: 198 }} mt={2}>
        <Box flex={1}>
          <YBInput
            type="number"
            label={t('createUniverseV2.instanceSettings.provisionedIopsPerNode')}
            fullWidth
            disabled={disableIops || disabled}
            slotProps={{
              htmlInput: {
                min: 1,
                'data-testid': `VolumeInfoField-${dataTag}-DiskIopsInput`,
                disabled: disableIops || disabled
              }
            }}
            value={convertToString(fieldValue?.diskIops ?? '')}
            onChange={(event) => onDiskIopsChanged(event.target.value)}
            onBlur={resetThroughput}
            inputMode="numeric"
            dataTestId={`VolumeInfoField-${dataTag}-DiskIopsInput`}
          />
        </Box>
      </Box>
    );
  };

  const renderThroughput = () => {
    if (
      fieldValue?.storageType &&
      ![
        StorageType.GP3,
        StorageType.UltraSSD_LRS,
        StorageType.PremiumV2_LRS,
        StorageType.Hyperdisk_Balanced
      ].includes(fieldValue?.storageType)
    ) {
      return null;
    }

    return (
      <Box display="flex" flexDirection="column" mt={2}>
        <Box display="flex">
          <Box>
            <YBLabel>{t('createUniverseV2.instanceSettings.provisionedThroughputPerNode')}</YBLabel>
          </Box>
        </Box>
        <Box display="flex" width="100%">
          <Box display="flex" sx={{ width: 198 }}>
            <YBInput
              type="number"
              fullWidth
              disabled={disableThroughput || disabled}
              slotProps={{
                htmlInput: {
                  min: 1,
                  'data-testid': `VolumeInfoField-${dataTag}-ThroughputInput`,
                  disabled: disableThroughput || disabled
                }
              }}
              value={convertToString(fieldValue?.throughput ?? '')}
              onChange={(event) => onThroughputChange(event.target.value)}
              inputMode="numeric"
              dataTestId={`VolumeInfoField-${dataTag}-ThroughputInput`}
            />
          </Box>
          <Box
            ml={2}
            display="flex"
            alignItems="center"
            sx={(theme) => ({
              marginLeft: theme.spacing(2),
              alignSelf: 'flex-end',
              marginBottom: 1
            })}
          >
            {t('createUniverseV2.instanceSettings.throughputUnit')}
          </Box>
        </Box>
      </Box>
    );
  };

  const isGcpDedicatedUniverse = provider?.code === CloudType.gcp && useDedicatedNodes;
  return (
    <Controller
      control={control}
      name={UPDATE_FIELD}
      render={() => (
        <>
          {fieldValue && (
            <Box display="flex" width="100%" flexDirection="column">
              <Box display="flex" width="100%" flexDirection="column">
                {renderVolumeInfo()}
                {!isGcpDedicatedUniverse && renderStorageType()}
              </Box>

              {fieldValue.storageType && !isGcpDedicatedUniverse && (
                <Box display="flex" width="100%" flexDirection="column">
                  {renderDiskIops()}
                  {renderThroughput()}
                </Box>
              )}
            </Box>
          )}
        </>
      )}
    />
  );
};
