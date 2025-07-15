import { FC, useContext, useEffect } from 'react';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { Box, makeStyles, MenuItem } from '@material-ui/core';
import { YBInput, YBLabel, YBSelect } from '@yugabyte-ui-library/core';
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
} from './VolumeInfoFieldHelper';
import { isEphemeralAwsStorageInstance } from '../instance-type/InstanceTypeFieldHelper';
import {
  CloudType,
  StorageType,
  VolumeType
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { IsOsPatchingEnabled } from '@app/components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import { InstanceSettingProps } from '../../steps/hardware-settings/dtos';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { ReactComponent as Close } from '@app/redesign/assets/close.svg';
import {
  CPU_ARCHITECTURE_FIELD,
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD
} from '../FieldNames';

interface VolumeInfoFieldProps {
  isMaster?: boolean;
  maxVolumeCount: number;
  disabled: boolean;
}

const useStyles = makeStyles((theme) => ({
  volumeInfoTextField: {
    width: theme.spacing(15.5)
  },
  storageTypeLabelField: {
    minWidth: theme.spacing(21.25)
  },
  storageTypeSelectField: {
    maxWidth: theme.spacing(35.25),
    minWidth: theme.spacing(30)
  },
  unitLabelField: {
    marginLeft: theme.spacing(2),
    alignSelf: 'flex-end',
    marginBottom: 8
  },
  overrideMuiHelperText: {
    '& .MuiFormHelperText-root': {
      color: theme.palette.orange[500]
    }
  },
  coolDownTooltip: {
    marginLeft: theme.spacing(1),
    alignSelf: 'center'
  },
  warningStorageLabelField: {
    marginTop: theme.spacing(2),
    alignItems: 'flex-start'
  }
}));

export const VolumeInfoField: FC<VolumeInfoFieldProps> = ({
  isMaster,
  maxVolumeCount,
  disabled
}) => {
  //   const { control, setValue, watch } = useFormContext<UniverseFormData>();
  const classes = useStyles();
  const { t } = useTranslation();
  const dataTag = isMaster ? 'Master' : 'TServer';

  //watchers

  // watchers
  const { watch, control, setValue } = useFormContext<InstanceSettingProps>();
  const [{ generalSettings, nodesAvailabilitySettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const useDedicatedNodes = nodesAvailabilitySettings?.useDedicatedNodes;
  const provider = generalSettings?.providerConfiguration;
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

  //fetch run time configs
  const {
    data: providerRuntimeConfigs,
    refetch: providerConfigsRefetch
  } = useQuery(QUERY_KEY.fetchProviderRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, provider?.uuid)
  );

  // Update field is based on master or tserver field in dedicated mode
  const UPDATE_FIELD = isMaster ? MASTER_DEVICE_INFO_FIELD : DEVICE_INFO_FIELD;

  const isOsPatchingEnabled = IsOsPatchingEnabled();

  //get instance details
  const { data: instanceTypes } = useQuery(
    [QUERY_KEY.getInstanceTypes, provider?.uuid, isOsPatchingEnabled ? cpuArch : null],
    () => api.getInstanceTypes(provider?.uuid, [], isOsPatchingEnabled ? cpuArch : null),
    { enabled: !!provider?.uuid }
  );
  const instance = instanceTypes?.find((item) => item.instanceTypeCode === instanceType);

  // Update volume info after instance changes
  // We need to have have 2 separate useEffects for instanceType and provider changes
  // once provider can be edited via the UI in case of primary cluster
  useEffect(() => {
    if (!instance) return;
    const getProviderRuntimeConfigs = async () => {
      const providerRuntimeConfigsRefetch = await providerConfigsRefetch();
      const deviceInfo = getDeviceInfoFromInstance(
        instance,
        providerRuntimeConfigsRefetch.isError
          ? providerRuntimeConfigs
          : providerRuntimeConfigsRefetch.data
      );

      //retain old volume size if its edit mode or not ephemeral storage
      if (
        fieldValue &&
        deviceInfo &&
        !isEphemeralAwsStorageInstance(instance) &&
        provider &&
        provider.code !== CloudType.onprem
      ) {
        deviceInfo.volumeSize = fieldValue.volumeSize;
        deviceInfo.numVolumes = fieldValue.numVolumes;
      }

      deviceInfo && setValue(UPDATE_FIELD, deviceInfo);
    };
    getProviderRuntimeConfigs();
  }, [instanceType, provider?.uuid]);

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
      provider &&
      ![CloudType.kubernetes, CloudType.gcp, CloudType.azu].includes(provider?.code);

    // Ephemeral instances volume information cannot be resized, refer to PLAT-16118
    const isEphemeralStorage =
      provider?.code === CloudType.aws && isEphemeralAwsStorageInstance(instance);

    const smartResizePossible =
      provider &&
      [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code) &&
      !isEphemeralAwsStorageInstance(instance) &&
      fieldValue?.storageType !== StorageType.Scratch;

    return (
      <Box display="flex" flexDirection="column">
        <Box display="flex">
          <Box>
            <YBLabel>{t('universeForm.instanceConfig.volumeInfoPerNode')}</YBLabel>
          </Box>
        </Box>
        <Box display="flex">
          <Box flex={1} className={classes.volumeInfoTextField} sx={{ width: 198 }}>
            <YBInput
              type="number"
              fullWidth
              disabled={fixedNumVolumes ?? numVolumesDisable ?? isEphemeralStorage ?? disabled}
              slotProps={{
                htmlInput: {
                  min: 1,
                  'data-testid': `VolumeInfoField-${dataTag}-VolumeInput`,
                  disabled
                }
              }}
              value={convertToString(fieldValue?.numVolumes ?? '')}
              onChange={(event) => onNumVolumesChanged(event.target.value)}
              inputMode="numeric"
              dataTestId={`VolumeInfoField-${dataTag}-VolumeInput`}
            />
          </Box>

          <Box
            display="flex"
            alignItems="center"
            justifyContent="center"
            px={1}
            flexShrink={1}
            sx={{ width: 48 }}
          >
            <Close />
          </Box>

          <Box
            display="flex"
            alignItems="flex-end"
            flex={1}
            className={classes.volumeInfoTextField}
            sx={{ width: 198 }}
          >
            <YBInput
              type="number"
              fullWidth
              disabled={isEphemeralStorage || fixedVolumeSize || volumeSizeDisable || disabled}
              slotProps={{
                htmlInput: {
                  min: 1,
                  'data-testid': `VolumeInfoField-${dataTag}-VolumeSizeInput`,
                  disabled
                }
              }}
              className={classes.overrideMuiHelperText}
              value={convertToString(fieldValue?.volumeSize ?? '')}
              onChange={(event) => onVolumeSizeChanged(event.target.value)}
              onBlur={resetThroughput}
              inputMode="numeric"
              dataTestId={`VolumeInfoField-${dataTag}-VolumeSizeInput`}
            />
          </Box>

          <Box display="flex" alignItems="center" className={classes.unitLabelField}>
            {provider?.code === CloudType.kubernetes
              ? t('universeForm.instanceConfig.k8VolumeSizeUnit')
              : t('universeForm.instanceConfig.volumeSizeUnit')}
          </Box>
        </Box>
      </Box>
    );
  };

  const renderStorageType = () => {
    if (
      (provider && [CloudType.gcp, CloudType.azu].includes(provider?.code)) ||
      (volumeType === VolumeType.EBS && provider?.code === CloudType.aws)
    )
      return (
        <Box display="flex" sx={{ width: 198 }} mt={2}>
          <Box flex={1}>
            <YBSelect
              fullWidth
              label={
                provider?.code === CloudType.aws
                  ? t('universeForm.instanceConfig.ebs')
                  : t('universeForm.instanceConfig.ssd')
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
            >
              {getStorageTypeOptions(provider?.code, providerRuntimeConfigs).map((item) => (
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
      fieldValue?.storageType &&
      ![
        StorageType.IO1,
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
            type="number"
            label={t('universeForm.instanceConfig.provisionedIopsPerNode')}
            fullWidth
            disabled={disableIops || disabled}
            slotProps={{
              htmlInput: {
                min: 1,
                'data-testid': `VolumeInfoField-${dataTag}-DiskIopsInput`,
                disabled
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
    )
      return null;

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
              disabled={disableThroughput || disabled}
              slotProps={{
                htmlInput: {
                  min: 1,
                  'data-testid': `VolumeInfoField-${dataTag}-ThroughputInput`,
                  disabled
                }
              }}
              value={convertToString(fieldValue?.throughput ?? '')}
              onChange={(event) => onThroughputChange(event.target.value)}
              inputMode="numeric"
              dataTestId={`VolumeInfoField-${dataTag}-ThroughputInput`}
            />
          </Box>
          <Box ml={2} display="flex" alignItems="center" className={classes.unitLabelField}>
            {t('universeForm.instanceConfig.throughputUnit')}
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
                <>{renderVolumeInfo()}</>
                <>{!isGcpDedicatedUniverse && <>{renderStorageType()}</>}</>
              </Box>

              {fieldValue.storageType && (
                <Box display="flex" width="100%" flexDirection="column">
                  {!isGcpDedicatedUniverse && (
                    <>
                      {renderDiskIops()}
                      {renderThroughput()}
                    </>
                  )}
                </Box>
              )}
            </Box>
          )}
        </>
      )}
    />
  );
};
