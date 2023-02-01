import React, { FC, useRef } from 'react';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box, Grid, MenuItem } from '@material-ui/core';
import { YBInput, YBLabel, YBSelect } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import {
  getStorageTypeOptions,
  getDeviceInfoFromInstance,
  getMinDiskIops,
  getMaxDiskIops,
  getIopsByStorageType,
  getThroughputByStorageType,
  getThroughputByIops
} from './VolumeInfoFieldHelper';
import { isEphemeralAwsStorageInstance } from '../InstanceTypeField/InstanceTypeFieldHelper';
import { CloudType, StorageType, UniverseFormData, VolumeType } from '../../../utils/dto';
import { PROVIDER_FIELD, DEVICE_INFO_FIELD, INSTANCE_TYPE_FIELD } from '../../../utils/constants';

interface VolumeInfoFieldProps {
  isEditMode: boolean;
  isPrimary: boolean;
  disableIops: boolean;
  disableThroughput: boolean;
  disableStorageType: boolean;
  disableVolumeSize: boolean;
  disableNumVolumes: boolean;
}

export const VolumeInfoField: FC<VolumeInfoFieldProps> = ({
  isEditMode,
  isPrimary,
  disableIops,
  disableThroughput,
  disableStorageType,
  disableVolumeSize,
  disableNumVolumes
}) => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const instanceTypeChanged = useRef(false);

  //watchers
  const fieldValue = useWatch({ name: DEVICE_INFO_FIELD });
  const instanceType = useWatch({ name: INSTANCE_TYPE_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });

  //get instance details
  const { data: instanceTypes } = useQuery(
    [QUERY_KEY.getInstanceTypes, provider?.uuid],
    () => api.getInstanceTypes(provider?.uuid),
    { enabled: !!provider?.uuid }
  );
  const instance = instanceTypes?.find((item) => item.instanceTypeCode === instanceType);

  //update volume info after istance changes
  useUpdateEffect(() => {
    if (!instance) return;

    let deviceInfo = getDeviceInfoFromInstance(instance);

    //retain old volume size if its edit mode or not ephemeral storage
    if (fieldValue && deviceInfo && !isEphemeralAwsStorageInstance(instance) && isEditMode) {
      deviceInfo.volumeSize = fieldValue.volumeSize;
      deviceInfo.numVolumes = fieldValue.numVolumes;
    }

    setValue(DEVICE_INFO_FIELD, deviceInfo ?? null);
  }, [instanceType]);

  //mark instance changed once only in edit mode
  useUpdateEffect(() => {
    if (isEditMode) instanceTypeChanged.current = true;
  }, [instanceType]);

  const convertToString = (str: string) => str?.toString() ?? '';

  //reset methods
  const resetThroughput = () => {
    const { storageType, throughput, diskIops } = fieldValue;
    if ([StorageType.IO1, StorageType.GP3, StorageType.UltraSSD_LRS].includes(storageType)) {
      //resetting throughput
      const throughputVal = getThroughputByIops(Number(throughput), diskIops, storageType);
      setValue(DEVICE_INFO_FIELD, { ...fieldValue, throughput: throughputVal });
    }
  };

  //field actions
  const onStorageTypeChanged = (storageType: StorageType) => {
    const throughput = getThroughputByStorageType(storageType);
    const diskIops = getIopsByStorageType(storageType);
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, throughput, diskIops, storageType });
  };

  const onVolumeSizeChanged = (value: any) => {
    const { storageType, diskIops } = fieldValue;
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, volumeSize: Number(value) });
    if (storageType === StorageType.UltraSSD_LRS) {
      onDiskIopsChanged(diskIops);
    }
  };

  const onDiskIopsChanged = (value: any) => {
    const { storageType, volumeSize } = fieldValue;
    const maxDiskIops = getMaxDiskIops(storageType, volumeSize);
    const minDiskIops = getMinDiskIops(storageType, volumeSize);
    const diskIops = Math.max(minDiskIops, Math.min(maxDiskIops, Number(value)));
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, diskIops });
  };

  const onThroughputChange = (value: any) => {
    const { storageType, diskIops } = fieldValue;
    const throughput = getThroughputByIops(Number(value), diskIops, storageType);
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, throughput });
  };

  const onNumVolumesChanged = (numVolumes: any) => {
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, numVolumes: Number(numVolumes) });
  };

  //render
  if (!instance) return null;

  const { volumeDetailsList } = instance.instanceTypeDetails;
  const { volumeType } = volumeDetailsList[0];

  if (![VolumeType.EBS, VolumeType.SSD, VolumeType.NVME].includes(volumeType)) return null;

  const renderVolumeInfo = () => {
    const fixedVolumeSize =
      [VolumeType.SSD, VolumeType.NVME].includes(volumeType) &&
      fieldValue?.storageType === StorageType.Scratch &&
      ![CloudType.kubernetes, CloudType.azu].includes(provider?.code);

    const fixedNumVolumes =
      [VolumeType.SSD, VolumeType.NVME].includes(volumeType) &&
      ![CloudType.kubernetes, CloudType.gcp, CloudType.azu].includes(provider?.code);

    const smartResizePossible =
      [CloudType.aws, CloudType.gcp].includes(provider?.code) &&
      !isEphemeralAwsStorageInstance(instance) &&
      fieldValue?.storageType !== StorageType.Scratch &&
      isPrimary;

    return (
      <Box display="flex">
        <Box display="flex">
          <YBLabel dataTestId="VolumeInfoField-Label">
            {t('universeForm.instanceConfig.volumeInfo')}
          </YBLabel>
        </Box>

        <Box display="flex" flex={1}>
          <Box flex={1}>
            <YBInput
              type="number"
              fullWidth
              disabled={fixedNumVolumes || !instanceTypeChanged.current || disableNumVolumes}
              inputProps={{ min: 1, 'data-testid': 'VolumeInfoField-VolumeInput' }}
              value={convertToString(fieldValue.numVolumes)}
              onChange={(event) => onNumVolumesChanged(event.target.value)}
              inputMode="numeric"
            />
          </Box>

          <Box display="flex" alignItems="center" px={1} flexShrink={1}>
            x
          </Box>

          <Box flex={1}>
            <YBInput
              type="number"
              fullWidth
              disabled={
                fixedVolumeSize ||
                (provider?.code !== CloudType.kubernetes &&
                  !smartResizePossible &&
                  !instanceTypeChanged.current) ||
                disableVolumeSize
              }
              inputProps={{ min: 1, 'data-testid': 'VolumeInfoField-VolumeSizeInput' }}
              value={convertToString(fieldValue.volumeSize)}
              onChange={(event) => onVolumeSizeChanged(event.target.value)}
              onBlur={resetThroughput}
              inputMode="numeric"
            />
          </Box>
        </Box>
      </Box>
    );
  };

  const renderStorageType = () => {
    if (
      [CloudType.gcp, CloudType.azu].includes(provider?.code) ||
      (volumeType === VolumeType.EBS && provider?.code === CloudType.aws)
    )
      return (
        <Box display="flex">
          <YBLabel dataTestId="VolumeInfoField-StorageTypeLabel">
            {provider?.code === CloudType.aws
              ? t('universeForm.instanceConfig.ebs')
              : t('universeForm.instanceConfig.ssd')}
          </YBLabel>
          <Box flex={1}>
            <YBSelect
              fullWidth
              disabled={disableStorageType}
              value={fieldValue.storageType}
              inputProps={{ 'data-testid': 'VolumeInfoField-StorageTypeSelect' }}
              onChange={(event) =>
                onStorageTypeChanged((event?.target.value as unknown) as StorageType)
              }
            >
              {getStorageTypeOptions(provider?.code).map((item) => (
                <MenuItem key={item.value} value={item.value}>
                  {item.label}
                </MenuItem>
              ))}
            </YBSelect>
          </Box>
        </Box>
      );

    return null;
  };

  const renderDiskIops = () => {
    if (
      ![StorageType.IO1, StorageType.GP3, StorageType.UltraSSD_LRS].includes(fieldValue.storageType)
    )
      return null;

    return (
      <Box display="flex">
        <YBLabel dataTestId="VolumeInfoField-DiskIopsLabel">
          {t('universeForm.instanceConfig.provisionedIops')}
        </YBLabel>
        <Box flex={1}>
          <YBInput
            type="number"
            fullWidth
            disabled={disableIops}
            inputProps={{ min: 1, 'data-testid': 'VolumeInfoField-DiskIopsInput' }}
            value={convertToString(fieldValue.diskIops)}
            onChange={(event) => onDiskIopsChanged(event.target.value)}
            onBlur={resetThroughput}
            inputMode="numeric"
          />
        </Box>
      </Box>
    );
  };

  const renderThroughput = () => {
    if (![StorageType.GP3, StorageType.UltraSSD_LRS].includes(fieldValue.storageType)) return null;
    return (
      <Box display="flex">
        <YBLabel dataTestId="VolumeInfoField-ThroughputLabel">
          {' '}
          {t('universeForm.instanceConfig.provisionedThroughput')}
        </YBLabel>
        <Box flex={1}>
          <YBInput
            type="number"
            fullWidth
            disabled={disableThroughput}
            inputProps={{ min: 1, 'data-testid': 'VolumeInfoField-ThroughputInput' }}
            value={convertToString(fieldValue.throughput)}
            onChange={(event) => onThroughputChange(event.target.value)}
            inputMode="numeric"
          />
        </Box>
      </Box>
    );
  };

  return (
    <Controller
      control={control}
      name={DEVICE_INFO_FIELD}
      render={() => (
        <>
          {fieldValue && (
            <Box display="flex" width="100%" flexDirection="column">
              <Box>
                <Grid container spacing={2}>
                  <Grid item lg={12} xs={12}>
                    {renderVolumeInfo()}
                  </Grid>

                  <Grid item lg={12} xs={12}>
                    {renderStorageType()}
                  </Grid>
                </Grid>
              </Box>

              {fieldValue.storageType && (
                <Box mt={1}>
                  <Grid container spacing={2}>
                    <Grid item lg={6} sm={12}>
                      {renderDiskIops()}
                    </Grid>

                    <Grid item lg={6} sm={12}>
                      {renderThroughput()}
                    </Grid>
                  </Grid>
                </Box>
              )}
            </Box>
          )}
        </>
      )}
    />
  );
};
