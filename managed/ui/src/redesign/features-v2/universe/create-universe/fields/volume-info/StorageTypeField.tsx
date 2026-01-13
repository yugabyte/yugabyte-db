import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { YBInput, YBLabel, YBSelect, mui } from '@yugabyte-ui-library/core';
import { IsOsPatchingEnabled } from '@app/components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import {
  getThroughputByStorageType,
  getStorageTypeOptions,
  getIopsByStorageType,
  useVolumeControls,
  getMaxDiskIops,
  getMinDiskIops,
  getThroughputByIops
} from '@app/redesign/features-v2/universe/create-universe/fields/volume-info/VolumeInfoFieldHelper';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import { StorageType, CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import {
  CPU_ARCHITECTURE_FIELD,
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  MASTER_DEVICE_INFO_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

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

interface StorageTypeFieldProps {
  disabled: boolean;
  provider?: ProviderType;
}

const { Box, MenuItem } = mui;

export const StorageTypeField: FC<StorageTypeFieldProps> = ({ disabled, provider }) => {
  const { t } = useTranslation();

  //fetch run time configs
  const { data: providerRuntimeConfigs } = useQuery(QUERY_KEY.fetchProviderRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, provider?.uuid)
  );

  // watchers
  const { watch, setValue } = useFormContext<InstanceSettingProps>();
  const fieldValue = watch(DEVICE_INFO_FIELD);
  const masterFieldValue = watch(MASTER_DEVICE_INFO_FIELD);
  const instanceType = watch(INSTANCE_TYPE_FIELD);
  const cpuArch = watch(CPU_ARCHITECTURE_FIELD);
  const { disableStorageType } = useVolumeControls();

  //field actions
  const onStorageTypeChanged = (storageType: StorageType) => {
    if (!fieldValue || !masterFieldValue) return;
    const throughput = getThroughputByStorageType(storageType);
    const diskIops = getIopsByStorageType(storageType);
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, throughput, diskIops, storageType });
    setValue(MASTER_DEVICE_INFO_FIELD, {
      ...masterFieldValue,
      throughput,
      diskIops,
      storageType
    });
  };

  // Update storage type to persistent when instance is changed in either TServer or Master
  useUpdateEffect(() => {
    const storageType: StorageType = StorageType.Persistent;
    const throughput = getThroughputByStorageType(storageType);
    const diskIops = getIopsByStorageType(storageType);
    if (
      fieldValue?.storageType === StorageType.Persistent &&
      masterFieldValue?.storageType === StorageType.Scratch
    ) {
      setValue(MASTER_DEVICE_INFO_FIELD, {
        ...masterFieldValue,
        throughput,
        diskIops,
        storageType
      });
    }
    if (
      fieldValue?.storageType === StorageType.Scratch &&
      masterFieldValue?.storageType === StorageType.Persistent
    ) {
      setValue(DEVICE_INFO_FIELD, {
        ...masterFieldValue,
        throughput,
        diskIops,
        storageType
      });
    }
  }, [
    fieldValue?.storageType,
    masterFieldValue?.storageType,
    fieldValue?.throughput,
    masterFieldValue?.throughput
  ]);

  const convertToString = (str: string | number) => str?.toString() ?? '';

  const isOsPatchingEnabled = IsOsPatchingEnabled();

  //get instance details
  const { data: instanceTypes } = useQuery(
    [QUERY_KEY.getInstanceTypes, provider?.uuid, isOsPatchingEnabled ? cpuArch : null],
    () => api.getInstanceTypes(provider?.uuid, [], isOsPatchingEnabled ? cpuArch : null),
    { enabled: !!provider?.uuid }
  );

  const instance = instanceTypes?.find((item) => item.instanceTypeCode === instanceType);
  if (!instance) return null;

  const storageType = fieldValue?.storageType;

  const onDiskIopsChanged = (value: any) => {
    if (!fieldValue || !masterFieldValue) return;
    const { storageType, volumeSize } = fieldValue;
    if (!storageType || !volumeSize) return;
    const maxDiskIops = getMaxDiskIops(storageType, volumeSize);
    const minDiskIops = getMinDiskIops(storageType, volumeSize);
    const diskIops = Math.max(minDiskIops, Math.min(maxDiskIops, Number(value)));
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, diskIops });
    setValue(MASTER_DEVICE_INFO_FIELD, {
      ...masterFieldValue,
      diskIops
    });
  };

  const onThroughputChange = (value: any) => {
    if (!fieldValue || !masterFieldValue) return;
    const { storageType, diskIops } = fieldValue;
    if (!diskIops || !storageType) return;
    const throughput = getThroughputByIops(Number(value), diskIops, storageType);
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, throughput });
    setValue(MASTER_DEVICE_INFO_FIELD, {
      ...masterFieldValue,
      throughput
    });
  };

  const renderStorageType = () => {
    if (provider && [CloudType.gcp, CloudType.azu].includes(provider?.code))
      return (
        <Box display="flex" sx={{ width: 198 }}>
          <YBSelect
            label={
              provider?.code === CloudType.aws
                ? t('universeForm.instanceConfig.ebs')
                : t('universeForm.instanceConfig.ssd')
            }
            fullWidth
            disabled={disableStorageType || disabled}
            value={storageType}
            slotProps={{
              htmlInput: {
                min: 1,
                'data-testid': 'StorageTypeField-Common-StorageTypeSelect'
              }
            }}
            onChange={(event) =>
              onStorageTypeChanged((event?.target.value as unknown) as StorageType)
            }
            dataTestId="StorageTypeField-Common-StorageTypeSelect"
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
    )
      return null;

    return (
      <Box display="flex" sx={{ width: 198 }} mt={2}>
        <Box flex={1}>
          <YBInput
            label={t('universeForm.instanceConfig.provisionedIopsPerNode')}
            type="number"
            fullWidth
            slotProps={{
              htmlInput: { min: 1, 'data-testid': `StorageTypeField-DiskIopsInput`, disabled }
            }}
            value={convertToString(fieldValue?.diskIops ?? '')}
            onChange={(event) => onDiskIopsChanged(event.target.value)}
            inputMode="numeric"
            disabled={disabled}
            dataTestId="StorageTypeField-DiskIopsInput"
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
            <YBLabel>{t('universeForm.instanceConfig.provisionedThroughputPerNode')}</YBLabel>
          </Box>
        </Box>
        <Box display="flex" width="100%">
          <Box display="flex" sx={{ width: 198 }}>
            <YBInput
              type="number"
              fullWidth
              slotProps={{
                htmlInput: { min: 1, 'data-testid': `StorageTypeField-ThroughputInput`, disabled }
              }}
              value={convertToString(fieldValue?.throughput ?? '')}
              onChange={(event) => onThroughputChange(event.target.value)}
              inputMode="numeric"
              disabled={disabled}
              dataTestId="StorageTypeField-ThroughputInput"
            />
          </Box>
          <Box
            component="span"
            sx={(theme) => ({
              marginLeft: theme.spacing(2),
              alignSelf: 'flex-end',
              marginBottom: 8
            })}
          >
            {t('universeForm.instanceConfig.throughputUnit')}
          </Box>
        </Box>
      </Box>
    );
  };

  return (
    <Box display="flex" width="100%" flexDirection="column">
      <Box>{renderStorageType()}</Box>
      {fieldValue?.storageType && (
        <Box>
          {renderDiskIops()}
          {renderThroughput()}
        </Box>
      )}
    </Box>
  );
};
