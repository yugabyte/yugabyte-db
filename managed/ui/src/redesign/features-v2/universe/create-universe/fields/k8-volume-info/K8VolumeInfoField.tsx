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
}

export const K8VolumeInfoField = ({
  isMaster,
  disableVolumeSize,
  maxVolumeCount,
  disabled,
  provider
}: K8VolumeInfoFieldProps): ReactElement => {
  const { watch, control, setValue } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation();

  const nodeTypeTag = isMaster ? NodeType.Master : NodeType.TServer;
  const fieldValue = isMaster ? watch(MASTER_DEVICE_INFO_FIELD) : watch(DEVICE_INFO_FIELD);
  const UPDATE_FIELD = isMaster ? MASTER_DEVICE_INFO_FIELD : DEVICE_INFO_FIELD;
  // To set value based on master or tserver field in dedicated mode
  const INSTANCE_TYPE_UPDATE_FIELD = isMaster ? MASTER_INSTANCE_TYPE_FIELD : INSTANCE_TYPE_FIELD;
  const convertToString = (str: string | number) => str?.toString() ?? '';

  //fetch run time configs
  const { providerRuntimeConfigs } = useRuntimeConfigValues(provider?.uuid);

  useEffect(() => {
    const updateDeviceInfo = () => {
      const deviceInfo = getK8DeviceInfo(providerRuntimeConfigs);
      setValue(UPDATE_FIELD, deviceInfo);
    };
    !fieldValue && updateDeviceInfo();
    setValue(INSTANCE_TYPE_UPDATE_FIELD, null);
  }, []);

  const onVolumeSizeChanged = (value: any) => {
    if (!fieldValue) return;
    const defaults = getK8DeviceInfo(providerRuntimeConfigs);
    const dv = Number(defaults.volumeSize);
    const defaultSize = Math.max(
      1,
      Number.isFinite(dv) && dv > 0
        ? dv
        : fieldValue.volumeSize && fieldValue.volumeSize > 0
          ? fieldValue.volumeSize
          : 1
    );
    const volumeSize = parsePositiveIntegerInput(String(value), defaultSize);
    setValue(UPDATE_FIELD, { ...fieldValue, volumeSize });
  };

  const onNumVolumesChanged = (numVolumes: any) => {
    if (!fieldValue) return;
    const defaults = getK8DeviceInfo(providerRuntimeConfigs);
    const dnv = Number(defaults.numVolumes);
    const defaultNum = Math.max(
      1,
      Number.isFinite(dnv) && dnv > 0
        ? dnv
        : fieldValue.numVolumes && fieldValue.numVolumes > 0
          ? fieldValue.numVolumes
          : 1
    );
    const volumeCount = parsePositiveIntegerInput(String(numVolumes), defaultNum, maxVolumeCount);
    setValue(UPDATE_FIELD, { ...fieldValue, numVolumes: volumeCount });
  };

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
                <YBLabel>
                  {t('createUniverseV2.instanceSettings.provisionedThroughputPerPod')}
                </YBLabel>
              </Box>
            </Box>
            <Box display="flex" width="100%">
              <Box display="flex" sx={{ width: 198 }}>
                <YBInput
                  type="number"
                  fullWidth
                  slotProps={{
                    htmlInput: {
                      min: 1,
                      'data-testid': `K8VolumeInfoField-${nodeTypeTag}-VolumeInput`,
                      disabled
                    }
                  }}
                  value={convertToString(fieldValue?.numVolumes ?? '')}
                  onChange={(event) => onNumVolumesChanged(event.target.value)}
                  inputMode="numeric"
                  disabled={disabled}
                  dataTestId={`K8VolumeInfoField-${nodeTypeTag}-VolumeInput`}
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

              <Box display="flex" alignItems="flex-end" flex={1} sx={{ width: 198 }}>
                <YBInput
                  type="number"
                  fullWidth
                  disabled={disableVolumeSize || disabled}
                  slotProps={{
                    htmlInput: {
                      min: 1,
                      'data-testid': `K8VolumeInfoField-${nodeTypeTag}-VolumeSizeInput`,
                      disabled
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
                  alignSelf: 'flex-end',
                  marginBottom: 1
                })}
              >
                {t('createUniverseV2.instanceSettings.k8VolumeSizeUnit')}
              </Box>
            </Box>
          </Box>
        );
      }}
    />
  );
};
