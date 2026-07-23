import { FC, useEffect } from 'react';
import { useQuery } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import {
  YBInput,
  YBLabel,
  YBSelect,
  YBHelper,
  YBHelperVariants,
  mui
} from '@yugabyte-ui-library/core';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import {
  getDeviceInfoFromInstance,
  getIopsByStorageType,
  getStorageTypeOptions,
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
  VolumeType,
  StorageTypeSelectableCloudTypes
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { isStorageTypeSelectableCloudType } from '@app/components/configRedesign/providerRedesign/utils';
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
import { parsePositiveIntegerInput } from '@app/redesign/features-v2/universe/create-universe/helpers/instanceNumericInput';

//icons
import Close from '@app/redesign/assets/close.svg';

const { Box, MenuItem, Link, styled } = mui;

const StyledLink = styled(Link)(({ theme }) => ({
  color: theme.palette.warning[900],
  textDecorationColor: theme.palette.warning[900],
  '&:hover': {
    color: theme.palette.warning[900],
    textDecoration: 'underline'
  }
}));

interface VolumeInfoFieldProps {
  /** When true, do not replace form deviceInfo with defaults from instance metadata (edit-universe hardware). */
  isEditMode?: boolean;
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
  isEditMode = false,
  isMaster,
  maxVolumeCount: _maxVolumeCount,
  disabled,
  provider,
  useDedicatedNodes,
  regions
}) => {
  const { t } = useTranslation();
  const dataTag = isMaster ? 'Master' : 'TServer';

  // watchers
  const {
    watch,
    control,
    setValue,
    getValues,
    formState: { errors, isSubmitted }
  } = useFormContext<InstanceSettingProps>();
  const fieldValue = isMaster ? watch(MASTER_DEVICE_INFO_FIELD) : watch(DEVICE_INFO_FIELD);
  const instanceType = isMaster ? watch(MASTER_INSTANCE_TYPE_FIELD) : watch(INSTANCE_TYPE_FIELD);
  const cpuArch = watch(CPU_ARCHITECTURE_FIELD);

  const deviceFieldErrors = (isMaster ? errors.masterDeviceInfo : errors.deviceInfo) as
    | {
        volumeSize?: { message?: string };
        numVolumes?: { message?: string };
        diskIops?: { message?: string };
        throughput?: { message?: string };
      }
    | undefined;

  // validate after submit so errors clear while typing
  const setDeviceInfo = (next: NonNullable<typeof fieldValue>) => {
    setValue(UPDATE_FIELD, next, { shouldValidate: isSubmitted, shouldDirty: true });
  };

  const {
    numVolumesDisable,
    volumeSizeDisable,
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

  // Update volume info after instance changes (create flow only; edit flow keeps API/universe values)
  useEffect(() => {
    if (isEditMode || !instance || !provider?.uuid) return;
    const updateDeviceInfo = () => {
      const deviceInfo = getDeviceInfoFromInstance(instance, providerRuntimeConfigs);
      deviceInfo && setValue(UPDATE_FIELD, deviceInfo);
    };
    !fieldValue && updateDeviceInfo();
    // eslint-disable-next-line react-hooks/exhaustive-deps -- hydrate only when instance/provider loads; omit fieldValue to avoid clobbering after user edits
  }, [instance, provider?.uuid, isEditMode]);

  const convertToString = (str: string | number) => str?.toString() ?? '';

  //field actions
  const onStorageTypeChanged = (storageType: StorageType) => {
    const current = getValues(UPDATE_FIELD);
    if (!current) return;
    const throughput = getThroughputByStorageType(storageType);
    const diskIops = getIopsByStorageType(storageType);
    setDeviceInfo({ ...current, throughput, diskIops, storageType });
  };

  const onVolumeSizeChanged = (value: any) => {
    const current = getValues(UPDATE_FIELD);
    if (!current || !instance) return;
    const volumeSize = parsePositiveIntegerInput(String(value));
    setDeviceInfo({
      ...current,
      volumeSize
    });
  };

  const onDiskIopsChanged = (value: any) => {
    const current = getValues(UPDATE_FIELD);
    if (!current) return;
    if (!current.storageType) return;
    const diskIops = parsePositiveIntegerInput(String(value ?? ''));
    setDeviceInfo({ ...current, diskIops });
  };

  const onThroughputChange = (value: any) => {
    const current = getValues(UPDATE_FIELD);
    if (!current) return;
    if (!current.storageType) return;
    const throughput = parsePositiveIntegerInput(String(value));
    setDeviceInfo({ ...current, throughput });
  };

  const onNumVolumesChanged = (numVolumes: any) => {
    const current = getValues(UPDATE_FIELD);
    if (!current || !instance) return;
    const volumeCount = parsePositiveIntegerInput(String(numVolumes));
    setDeviceInfo({ ...current, numVolumes: volumeCount });
  };

  //render
  if (!instance) return null;

  const { volumeDetailsList } = instance?.instanceTypeDetails;
  const { volumeType } = volumeDetailsList[0];

  if (![VolumeType.EBS, VolumeType.SSD, VolumeType.NVME].includes(volumeType)) return null;

  const storageTypeSelectVisible =
    isStorageTypeSelectableCloudType(provider?.code) ||
    (volumeType === VolumeType.EBS && provider?.code === CloudType.aws);

  const showEphemeralStorageWarning =
    (provider?.code === CloudType.aws && isEphemeralAwsStorageInstance(instance)) ||
    (provider?.code === CloudType.gcp && fieldValue?.storageType === StorageType.Scratch);

  const renderVolumeInfo = () => {
    // Checking if provider code is OnPrem as it is provisioned to fixed size
    // and cannot be changed on both edit and create mode
    const fixedVolumeSize =
      (fieldValue?.storageType === StorageType.Scratch && provider?.code === CloudType.gcp) ||
      provider?.code === CloudType.onprem;

    const fixedNumVolumes =
      [VolumeType.SSD, VolumeType.NVME].includes(volumeType) &&
      provider?.code &&
      ![CloudType.kubernetes, ...StorageTypeSelectableCloudTypes].includes(provider.code);

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
        <Box sx={{ gap: '16px', display: 'flex', alignItems: 'flex-start' }}>
          <Box flex={1} sx={{ width: 198 }}>
            <YBInput
              type="number"
              fullWidth
              disabled={fixedNumVolumes || numVolumesDisable || isEphemeralStorage || disabled}
              error={!!deviceFieldErrors?.numVolumes}
              helperText={deviceFieldErrors?.numVolumes?.message}
              slotProps={{
                htmlInput: {
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

          <Box
            display="flex"
            alignItems="center"
            justifyContent="center"
            sx={{ height: 40, flexShrink: 0 }}
          >
            <Close />
          </Box>

          <Box flex={1} sx={{ width: 198 }}>
            <YBInput
              type="number"
              fullWidth
              disabled={isEphemeralStorage || fixedVolumeSize || volumeSizeDisable || disabled}
              error={!!deviceFieldErrors?.volumeSize}
              helperText={deviceFieldErrors?.volumeSize?.message}
              slotProps={{
                htmlInput: {
                  'data-testid': `VolumeInfoField-${dataTag}-VolumeSizeInput`,
                  disabled: isEphemeralStorage || fixedVolumeSize || volumeSizeDisable || disabled
                }
              }}
              value={convertToString(fieldValue?.volumeSize ?? '')}
              onChange={(event) => onVolumeSizeChanged(event.target.value)}
              inputMode="numeric"
              dataTestId={`VolumeInfoField-${dataTag}-VolumeSizeInput`}
            />
          </Box>

          <Box display="flex" alignItems="center" sx={{ height: 40, flexShrink: 0 }}>
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
      isStorageTypeSelectableCloudType(provider?.code) ||
      (volumeType === VolumeType.EBS && provider?.code === CloudType.aws)
    ) {
      const isPremiumV2Storage = fieldValue?.storageType === StorageType.PremiumV2_LRS;
      const isHyperdisk =
        fieldValue?.storageType === StorageType.Hyperdisk_Balanced ||
        fieldValue?.storageType === StorageType.Hyperdisk_Extreme;

      return (
        <Box display="flex" flexDirection="column" mt={2}>
          <Box sx={{ width: 198 }}>
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
                onStorageTypeChanged(event?.target.value as unknown as StorageType)
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
          {isHyperdisk && (
            <Box mt={1}>
              <YBHelper variant={YBHelperVariants.WARNING}>
                <Trans>
                  {t('createUniverseV2.instanceSettings.hyperdiskStorageHelper')}
                  <StyledLink
                    underline="always"
                    href="https://docs.yugabyte.com/stable/deploy/checklist/#disks"
                    target="_blank"
                  ></StyledLink>
                </Trans>
              </YBHelper>
            </Box>
          )}
          {isPremiumV2Storage && !isHyperdisk && (
            <Box mt={1}>
              <YBHelper variant={YBHelperVariants.WARNING}>
                {t('createUniverseV2.instanceSettings.premiumv2StorageHelper')}
              </YBHelper>
            </Box>
          )}
          {showEphemeralStorageWarning && storageTypeSelectVisible && (
            <Box mt={1}>
              <YBHelper variant={YBHelperVariants.WARNING}>
                <Trans>
                  {t('createUniverseV2.instanceSettings.ephemeralStorageWarning')}
                  <StyledLink
                    underline="always"
                    href="https://docs.yugabyte.com/stable/deploy/checklist/#ephemeral-disks"
                    target="_blank"
                  ></StyledLink>
                </Trans>
              </YBHelper>
            </Box>
          )}
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
            error={!!deviceFieldErrors?.diskIops}
            helperText={deviceFieldErrors?.diskIops?.message}
            slotProps={{
              htmlInput: {
                'data-testid': `VolumeInfoField-${dataTag}-DiskIopsInput`,
                disabled: disableIops || disabled
              }
            }}
            value={convertToString(fieldValue?.diskIops ?? '')}
            onChange={(event) => onDiskIopsChanged(event.target.value)}
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
        <Box display="flex" width="100%" alignItems="flex-start">
          <Box display="flex" sx={{ width: 198 }}>
            <YBInput
              type="number"
              fullWidth
              disabled={disableThroughput || disabled}
              error={!!deviceFieldErrors?.throughput}
              helperText={deviceFieldErrors?.throughput?.message}
              slotProps={{
                htmlInput: {
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
              height: 40,
              flexShrink: 0
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
                {!isGcpDedicatedUniverse &&
                  showEphemeralStorageWarning &&
                  !storageTypeSelectVisible && (
                    <Box mt={2} sx={{ maxWidth: 480 }}>
                      <YBHelper variant={YBHelperVariants.WARNING}>
                        <Trans>
                          {t('createUniverseV2.instanceSettings.ephemeralStorageWarning')}
                          <StyledLink
                            underline="always"
                            href="https://docs.yugabyte.com/stable/deploy/checklist/#ephemeral-disks"
                            target="_blank"
                          ></StyledLink>
                        </Trans>
                      </YBHelper>
                    </Box>
                  )}
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
