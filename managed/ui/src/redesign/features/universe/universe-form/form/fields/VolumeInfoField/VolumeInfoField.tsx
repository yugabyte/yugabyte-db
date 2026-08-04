import { FC, useEffect, useRef } from 'react';
import { useQuery } from 'react-query';
import clsx from 'clsx';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box, Grid, makeStyles, MenuItem, Tooltip } from '@material-ui/core';
import {
  YBHelper,
  YBHelperVariants,
  YBInput,
  YBLabel,
  YBSelect
} from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
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
import { isEphemeralAwsStorageInstance } from '../InstanceTypeField/InstanceTypeFieldHelper';
import {
  CloudType,
  MasterPlacementMode,
  StorageType,
  UniverseFormData,
  UpdateActions,
  VolumeType
} from '../../../utils/dto';
import { IsOsPatchingEnabled } from '../../../../../../../components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import { isNonEmptyArray } from '../../../../../../../utils/ObjectUtils';
import {
  CPU_ARCHITECTURE_FIELD,
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  MASTER_PLACEMENT_FIELD,
  PROVIDER_FIELD
} from '../../../utils/constants';
import WarningIcon from '../../../../../../assets/info-message.svg?img';

interface VolumeInfoFieldProps {
  isEditMode: boolean;
  isPrimary: boolean;
  isViewMode: boolean;
  isMaster?: boolean;
  maxVolumeCount: number;
  updateOptions: string[];
  diffInHours: number | null;
  AwsCoolDownPeriod: number;
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
    alignSelf: 'center'
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
  isEditMode,
  isPrimary,
  isViewMode,
  isMaster,
  maxVolumeCount,
  updateOptions,
  diffInHours,
  AwsCoolDownPeriod
}) => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const classes = useStyles();
  const { t } = useTranslation();
  const instanceTypeChanged = useRef(false);
  const dataTag = isMaster ? 'Master' : 'TServer';

  //watchers
  const fieldValue = isMaster
    ? useWatch({ name: MASTER_DEVICE_INFO_FIELD })
    : useWatch({ name: DEVICE_INFO_FIELD });
  const instanceType = isMaster
    ? useWatch({ name: MASTER_INSTANCE_TYPE_FIELD })
    : useWatch({ name: INSTANCE_TYPE_FIELD });
  const cpuArch = useWatch({ name: CPU_ARCHITECTURE_FIELD });
  const masterPlacement = useWatch({ name: MASTER_PLACEMENT_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });

  const {
    numVolumesDisable,
    volumeSizeDisable,
    minVolumeSize,
    disableIops,
    disableThroughput,
    disableStorageType
  } = useVolumeControls(isEditMode, updateOptions);
  const isAwsNodeCoolingDown = diffInHours !== null && diffInHours < AwsCoolDownPeriod;

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
          : providerRuntimeConfigsRefetch.data,
        isEditMode,
        fieldValue
      );

      //retain old volume size if its edit mode or not ephemeral storage
      if (
        fieldValue &&
        deviceInfo &&
        !isEphemeralAwsStorageInstance(instance) &&
        isEditMode &&
        provider.code !== CloudType.onprem
      ) {
        deviceInfo.volumeSize = fieldValue.volumeSize;
        deviceInfo.numVolumes = fieldValue.numVolumes;
      }

      setValue(UPDATE_FIELD, deviceInfo ?? null);
    };
    getProviderRuntimeConfigs();
  }, [instanceType, provider?.uuid]);

  //mark instance changed once only in edit mode
  useUpdateEffect(() => {
    if (isEditMode) instanceTypeChanged.current = true;
  }, [instanceType]);

  const convertToString = (str: string) => str?.toString() ?? '';

  //reset methods
  const resetThroughput = () => {
    const { storageType, throughput, diskIops, volumeSize } = fieldValue;
    if (
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
    const throughput = getThroughputByStorageType(storageType);
    const diskIops = getIopsByStorageType(storageType);
    setValue(UPDATE_FIELD, { ...fieldValue, throughput, diskIops, storageType });
  };

  const onVolumeSizeChanged = (value: any) => {
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
      fieldValue.storageType === StorageType.UltraSSD_LRS ||
      fieldValue.storageType === StorageType.PremiumV2_LRS
    ) {
      onDiskIopsChanged(fieldValue.diskIops);
    }
  }, [fieldValue?.volumeSize]);

  const onDiskIopsChanged = (value: any) => {
    const { storageType, volumeSize } = fieldValue;
    const maxDiskIops = getMaxDiskIops(storageType, volumeSize);
    const minDiskIops = getMinDiskIops(storageType, volumeSize);
    const diskIops = Math.max(minDiskIops, Math.min(maxDiskIops, Number(value)));
    setValue(UPDATE_FIELD, { ...fieldValue, diskIops });
  };

  const onThroughputChange = (value: any) => {
    const { storageType, diskIops } = fieldValue;
    const throughput = getThroughputByIops(Number(value), diskIops, storageType);
    setValue(UPDATE_FIELD, { ...fieldValue, throughput });
  };

  const onNumVolumesChanged = (numVolumes: any) => {
    const volumeCount = Number(numVolumes) > maxVolumeCount ? maxVolumeCount : Number(numVolumes);
    setValue(UPDATE_FIELD, { ...fieldValue, numVolumes: volumeCount });
  };

  //render
  if (!instance) return null;

  const { volumeDetailsList } = instance?.instanceTypeDetails;
  const { volumeType } = volumeDetailsList[0];

  if (![VolumeType.EBS, VolumeType.SSD, VolumeType.NVME].includes(volumeType)) return null;

  const renderVolumeInfo = () => {
    const isAWSProvider = provider?.code === CloudType.aws;
    const isSmartResize =
      isNonEmptyArray(updateOptions) &&
      (updateOptions.includes(UpdateActions.SMART_RESIZE) ||
        updateOptions.includes(UpdateActions.SMART_RESIZE_NON_RESTART));

    // Checking if provider code is OnPrem as it is provisioned to fixed size
    // and cannot be changed on both edit and create mode
    const fixedVolumeSize =
      (fieldValue?.storageType === StorageType.Scratch && provider?.code === CloudType.gcp) ||
      provider?.code === CloudType.onprem;

    const fixedNumVolumes =
      [VolumeType.SSD, VolumeType.NVME].includes(volumeType) &&
      ![CloudType.kubernetes, CloudType.gcp, CloudType.azu].includes(provider?.code);

    // Ephemeral instances volume information cannot be resized, refer to PLAT-16118
    const isEphemeralStorage =
      provider?.code === CloudType.aws && isEphemeralAwsStorageInstance(instance);

    const smartResizePossible =
      [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code) &&
      !isEphemeralAwsStorageInstance(instance) &&
      fieldValue?.storageType !== StorageType.Scratch;

    return (
      <Grid container spacing={2}>
        <Grid item lg={6}>
          <Box mt={2}>
            <Box display="flex">
              <Box display="flex">
                <YBLabel dataTestId="VolumeInfoField-Label">
                  {t('universeForm.instanceConfig.volumeInfo')}
                </YBLabel>
              </Box>

              <Box display="flex">
                <Box flex={1} className={classes.volumeInfoTextField}>
                  <YBInput
                    type="number"
                    fullWidth
                    disabled={
                      fixedNumVolumes || isViewMode || numVolumesDisable || isEphemeralStorage
                    }
                    inputProps={{ min: 1, 'data-testid': `VolumeInfoField-${dataTag}-VolumeInput` }}
                    value={convertToString(fieldValue.numVolumes)}
                    onChange={(event) => onNumVolumesChanged(event.target.value)}
                    inputMode="numeric"
                  />
                </Box>

                <Box display="flex" alignItems="center" px={1} flexShrink={1}>
                  x
                </Box>

                <Box flex={1} className={classes.volumeInfoTextField}>
                  <YBInput
                    type="number"
                    fullWidth
                    disabled={
                      isEphemeralStorage ||
                      fixedVolumeSize ||
                      isViewMode ||
                      (provider?.code !== CloudType.kubernetes &&
                        !smartResizePossible &&
                        isEditMode &&
                        !instanceTypeChanged.current) ||
                      volumeSizeDisable
                    }
                    inputProps={{
                      min: 1,
                      'data-testid': `VolumeInfoField-${dataTag}-VolumeSizeInput`
                    }}
                    className={classes.overrideMuiHelperText}
                    value={convertToString(fieldValue.volumeSize)}
                    onChange={(event) => onVolumeSizeChanged(event.target.value)}
                    onBlur={resetThroughput}
                    inputMode="numeric"
                  />
                </Box>
                <span className={classes.unitLabelField}>
                  {provider?.code === CloudType.kubernetes
                    ? t('universeForm.instanceConfig.k8VolumeSizeUnit')
                    : t('universeForm.instanceConfig.volumeSizeUnit')}
                </span>
                {isAWSProvider && isAwsNodeCoolingDown && !isSmartResize && (
                  <Box className={classes.coolDownTooltip}>
                    <Tooltip
                      title={t('universeForm.instanceConfig.cooldownHours')}
                      arrow
                      placement="top"
                    >
                      <img src={WarningIcon} alt="status" />
                    </Tooltip>
                  </Box>
                )}
              </Box>
            </Box>
          </Box>
        </Grid>
      </Grid>
    );
  };

  const renderStorageType = () => {
    const isPremiumV2Storage = fieldValue.storageType === StorageType.PremiumV2_LRS;
    const isHyperdisk =
      fieldValue.storageType === StorageType.Hyperdisk_Balanced ||
      fieldValue.storageType === StorageType.Hyperdisk_Extreme;
    if (
      [CloudType.gcp, CloudType.azu].includes(provider?.code) ||
      (volumeType === VolumeType.EBS && provider?.code === CloudType.aws)
    )
      return (
        <Grid container spacing={2}>
          <Grid item lg={6}>
            <Box mt={1}>
              <Box display="flex">
                <YBLabel
                  dataTestId="VolumeInfoField-StorageTypeLabel"
                  className={clsx(
                    classes.storageTypeLabelField,
                    (isPremiumV2Storage || isHyperdisk) && classes.warningStorageLabelField
                  )}
                >
                  {provider?.code === CloudType.aws
                    ? t('universeForm.instanceConfig.ebs')
                    : t('universeForm.instanceConfig.ssd')}
                </YBLabel>
                <Box flex={1}>
                  <YBSelect
                    className={classes.storageTypeSelectField}
                    disabled={disableStorageType || isViewMode}
                    value={fieldValue.storageType}
                    inputProps={{
                      min: 1,
                      'data-testid': `VolumeInfoField-${dataTag}-StorageTypeSelect`
                    }}
                    helperText={renderStorageHelperText(isPremiumV2Storage, isHyperdisk)}
                    onChange={(event) =>
                      onStorageTypeChanged((event?.target.value as unknown) as StorageType)
                    }
                  >
                    {getStorageTypeOptions(provider?.code, providerRuntimeConfigs).map((item) => (
                      <MenuItem key={item.value} value={item.value}>
                        {item.label}
                      </MenuItem>
                    ))}
                  </YBSelect>
                </Box>
              </Box>
            </Box>
          </Grid>
        </Grid>
      );

    return null;
  };

  const renderStorageHelperText = (isPremiumV2Storage: boolean, isHyperdisk: boolean) => {
    if (isHyperdisk)
      return (
        <YBHelper variant={YBHelperVariants.warning}>
          {t('universeForm.instanceConfig.hyperdiskStorage')}
        </YBHelper>
      );
    if (isPremiumV2Storage)
      return (
        <YBHelper variant={YBHelperVariants.warning}>
          {t('universeForm.instanceConfig.premiumv2Storage')}
        </YBHelper>
      );
    return null;
  };

  const renderDiskIops = () => {
    if (
      ![
        StorageType.IO1,
        StorageType.IO2,
        StorageType.GP3,
        StorageType.UltraSSD_LRS,
        StorageType.PremiumV2_LRS,
        StorageType.Hyperdisk_Balanced,
        StorageType.Hyperdisk_Extreme
      ].includes(fieldValue.storageType)
    )
      return null;

    return (
      <Grid container spacing={2}>
        <Grid item lg={6}>
          <Box display="flex" mt={2}>
            <YBLabel dataTestId="VolumeInfoField-DiskIopsLabel">
              {t('universeForm.instanceConfig.provisionedIops')}
            </YBLabel>
            <Box flex={1}>
              <YBInput
                type="number"
                fullWidth
                disabled={disableIops || isViewMode}
                inputProps={{ min: 1, 'data-testid': `VolumeInfoField-${dataTag}-DiskIopsInput` }}
                value={convertToString(fieldValue.diskIops)}
                onChange={(event) => onDiskIopsChanged(event.target.value)}
                onBlur={resetThroughput}
                inputMode="numeric"
              />
            </Box>
          </Box>
        </Grid>
      </Grid>
    );
  };

  const renderThroughput = () => {
    if (
      ![
        StorageType.GP3,
        StorageType.UltraSSD_LRS,
        StorageType.PremiumV2_LRS,
        StorageType.Hyperdisk_Balanced
      ].includes(fieldValue.storageType)
    )
      return null;
    return (
      <Grid container spacing={2}>
        <Grid item lg={6}>
          <Box display="flex" mt={1}>
            <YBLabel dataTestId="VolumeInfoField-ThroughputLabel">
              {t('universeForm.instanceConfig.provisionedThroughput')}
            </YBLabel>
            <Box flex={1}>
              <YBInput
                type="number"
                fullWidth
                disabled={disableThroughput || isViewMode}
                inputProps={{ min: 1, 'data-testid': `VolumeInfoField-${dataTag}-ThroughputInput` }}
                value={convertToString(fieldValue.throughput)}
                onChange={(event) => onThroughputChange(event.target.value)}
                inputMode="numeric"
              />
            </Box>
            <span className={classes.unitLabelField}>
              {t('universeForm.instanceConfig.throughputUnit')}
            </span>
          </Box>
        </Grid>
      </Grid>
    );
  };

  const isGcpDedicatedUniverse =
    provider?.code === CloudType.gcp && masterPlacement === MasterPlacementMode.DEDICATED;
  return (
    <Controller
      control={control}
      name={UPDATE_FIELD}
      render={() => (
        <>
          {fieldValue && (
            <Box display="flex" width="100%" flexDirection="column">
              <Box>
                {renderVolumeInfo()}
                {!isGcpDedicatedUniverse && <>{renderStorageType()}</>}
              </Box>

              {fieldValue.storageType && (
                <Box>
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
