import { ReactElement, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { YBLabel, YBInput, mui } from '@yugabyte-ui-library/core';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import { getK8DeviceInfo } from '@app/redesign/features-v2/universe/create-universe/fields/k8-volume-info/K8VolumeInfoFieldHelper';
import { NodeType } from '@app/redesign/utils/dtos';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import {
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';
import { parsePositiveIntegerInput } from '@app/redesign/features-v2/universe/create-universe/helpers/instanceNumericInput';

//icons
import Close from '@app/redesign/assets/close.svg';

const { Box } = mui;

interface K8VolumeInfoFieldProps {
  isMaster: boolean;
  disableVolumeSize: boolean;
  maxVolumeCount: number;
  disabled: boolean;
  provider?: ProviderType;
  isEditMode?: boolean;
  k8sOverrideEnabled?: boolean;
}

export const K8VolumeInfoField = ({
  isMaster,
  disableVolumeSize,
  maxVolumeCount: _maxVolumeCount,
  disabled,
  provider,
  isEditMode = false,
  k8sOverrideEnabled = false
}: K8VolumeInfoFieldProps): ReactElement => {
  const {
    watch,
    control,
    setValue,
    formState: { errors, isSubmitted }
  } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation();

  const nodeTypeTag = isMaster ? NodeType.Master : NodeType.TServer;
  const fieldValue = isMaster ? watch(MASTER_DEVICE_INFO_FIELD) : watch(DEVICE_INFO_FIELD);
  const UPDATE_FIELD = isMaster ? MASTER_DEVICE_INFO_FIELD : DEVICE_INFO_FIELD;
  const deviceFieldErrors = (isMaster ? errors.masterDeviceInfo : errors.deviceInfo) as
    | {
        volumeSize?: { message?: string };
        numVolumes?: { message?: string };
        storageClass?: { message?: string };
      }
    | undefined;
  // To set value based on master or tserver field in dedicated mode
  const INSTANCE_TYPE_UPDATE_FIELD = isMaster ? MASTER_INSTANCE_TYPE_FIELD : INSTANCE_TYPE_FIELD;
  const convertToString = (str: string | number) => str?.toString() ?? '';

  const setDeviceInfo = (next: NonNullable<typeof fieldValue>) => {
    setValue(UPDATE_FIELD, next, { shouldValidate: isSubmitted, shouldDirty: true });
  };

  //fetch run time configs
  const { providerRuntimeConfigs } = useRuntimeConfigValues(provider?.uuid);

  useEffect(() => {
    if (!fieldValue) {
      setValue(UPDATE_FIELD, getK8DeviceInfo(providerRuntimeConfigs));
    }
    setValue(INSTANCE_TYPE_UPDATE_FIELD, null);
    // eslint-disable-next-line react-hooks/exhaustive-deps -- hydrate once on mount
  }, []);

  const onVolumeSizeChanged = (value: any) => {
    if (!fieldValue) return;
    const volumeSize = parsePositiveIntegerInput(String(value));
    setDeviceInfo({ ...fieldValue, volumeSize });
  };

  const onNumVolumesChanged = (numVolumes: any) => {
    if (!fieldValue) return;
    const volumeCount = parsePositiveIntegerInput(String(numVolumes));
    setDeviceInfo({ ...fieldValue, numVolumes: volumeCount });
  };

  const onStorageClassChanged = (value: string) => {
    if (!fieldValue) return;
    setDeviceInfo({ ...fieldValue, storageClass: value });
  };

  const disableNumVolumes = disabled || (isEditMode && !k8sOverrideEnabled);

  return (
    <Controller
      name={UPDATE_FIELD}
      control={control}
      rules={{
        required: t('createUniverseV2.instanceSettings.validation.required', {
          field: t('createUniverseV2.instanceSettings.instanceType')
        }) as string
      }}
      render={() => {
        return (
          <Box display="flex" flexDirection="column">
            <Box display="flex">
              <Box>
                <YBLabel>{t('createUniverseV2.instanceSettings.volumeInfoPerPod')}</YBLabel>
              </Box>
            </Box>
            <Box display="flex" width="100%" alignItems="flex-start">
              <Box display="flex" sx={{ width: 198 }}>
                <YBInput
                  type="number"
                  fullWidth
                  error={!!deviceFieldErrors?.numVolumes}
                  helperText={deviceFieldErrors?.numVolumes?.message}
                  slotProps={{
                    htmlInput: {
                      'data-testid': `K8VolumeInfoField-${nodeTypeTag}-VolumeInput`,
                      disabled: disableNumVolumes
                    }
                  }}
                  value={convertToString(fieldValue?.numVolumes ?? '')}
                  onChange={(event) => onNumVolumesChanged(event.target.value)}
                  inputMode="numeric"
                  disabled={disableNumVolumes}
                  dataTestId={`K8VolumeInfoField-${nodeTypeTag}-VolumeInput`}
                />
              </Box>

              <Box
                display="flex"
                alignItems="center"
                justifyContent="center"
                px={1}
                flexShrink={0}
                sx={{ width: 48, height: 40 }}
              >
                <Close />
              </Box>

              <Box flex={1} sx={{ width: 198 }}>
                <YBInput
                  type="number"
                  fullWidth
                  disabled={disableVolumeSize || disabled}
                  error={!!deviceFieldErrors?.volumeSize}
                  helperText={deviceFieldErrors?.volumeSize?.message}
                  slotProps={{
                    htmlInput: {
                      'data-testid': `K8VolumeInfoField-${nodeTypeTag}-VolumeSizeInput`,
                      disabled: disableVolumeSize || disabled
                    }
                  }}
                  value={convertToString(fieldValue?.volumeSize ?? '')}
                  onChange={(event) => onVolumeSizeChanged(event.target.value)}
                  inputMode="numeric"
                  dataTestId={`K8VolumeInfoField-${nodeTypeTag}-VolumeSizeInput`}
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
                {t('createUniverseV2.instanceSettings.k8VolumeSizeUnit')}
              </Box>
            </Box>

            {k8sOverrideEnabled && (
              <Box display="flex" flexDirection="column" mt={2} sx={{ width: 198 }}>
                <YBInput
                  fullWidth
                  label={t('createUniverseV2.instanceSettings.storageClass')}
                  disabled={disabled}
                  error={!!deviceFieldErrors?.storageClass}
                  helperText={deviceFieldErrors?.storageClass?.message}
                  slotProps={{
                    htmlInput: {
                      'data-testid': `K8VolumeInfoField-${nodeTypeTag}-StorageClassInput`,
                      disabled
                    }
                  }}
                  value={fieldValue?.storageClass ?? ''}
                  onChange={(event) => onStorageClassChanged(event.target.value)}
                  dataTestId={`K8VolumeInfoField-${nodeTypeTag}-StorageClassInput`}
                />
              </Box>
            )}
          </Box>
        );
      }}
    />
  );
};
