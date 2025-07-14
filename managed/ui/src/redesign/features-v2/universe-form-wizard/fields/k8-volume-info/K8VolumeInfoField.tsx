import { ReactElement, useContext, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { Box, makeStyles } from '@material-ui/core';
import { YBLabel, YBInput } from '@yugabyte-ui-library/core';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import { getK8DeviceInfo } from './K8VolumeInfoFieldHelper';
import { NodeType } from '@app/redesign/utils/dtos';
import { InstanceSettingProps } from '../../steps/hardware-settings/dtos';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { ReactComponent as Close } from '@app/redesign/assets/close.svg';
import {
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD
} from '../FieldNames';

const useStyles = makeStyles((theme) => ({
  volumeInfoTextField: {
    width: theme.spacing(15.5)
  },
  unitLabelField: {
    marginLeft: theme.spacing(2),
    alignSelf: 'flex-end',
    marginBottom: 8
  }
}));

interface K8VolumeInfoFieldProps {
  isMaster: boolean;
  disableVolumeSize: boolean;
  maxVolumeCount: number;
  disabled: boolean;
}

export const K8VolumeInfoField = ({
  isMaster,
  disableVolumeSize,
  maxVolumeCount,
  disabled
}: K8VolumeInfoFieldProps): ReactElement => {
  const { watch, control, setValue } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });

  const [{ generalSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;
  const classes = useStyles();
  const nodeTypeTag = isMaster ? NodeType.Master : NodeType.TServer;

  // watchers
  const provider = generalSettings?.providerConfiguration;
  const fieldValue = isMaster ? watch(MASTER_DEVICE_INFO_FIELD) : watch(DEVICE_INFO_FIELD);
  const UPDATE_FIELD = isMaster ? MASTER_DEVICE_INFO_FIELD : DEVICE_INFO_FIELD;
  // To set value based on master or tserver field in dedicated mode
  const INSTANCE_TYPE_UPDATE_FIELD = isMaster ? MASTER_INSTANCE_TYPE_FIELD : INSTANCE_TYPE_FIELD;
  const convertToString = (str: string | number) => str?.toString() ?? '';

  //fetch run time configs
  const { refetch: providerConfigsRefetch } = useQuery(
    QUERY_KEY.fetchProviderRunTimeConfigs,
    () => api.fetchRunTimeConfigs(true, provider?.uuid),
    { enabled: !!provider?.uuid }
  );

  useEffect(() => {
    const getProviderRuntimeConfigs = async () => {
      const providerRuntimeRefetch = await providerConfigsRefetch();
      const deviceInfo = getK8DeviceInfo(providerRuntimeRefetch?.data);

      setValue(UPDATE_FIELD, deviceInfo);
    };
    getProviderRuntimeConfigs();
    setValue(INSTANCE_TYPE_UPDATE_FIELD, null);
  }, []);

  const onVolumeSizeChanged = (value: any) => {
    if (!fieldValue) return;
    setValue(UPDATE_FIELD, { ...fieldValue, volumeSize: Number(value) });
  };
  const onNumVolumesChanged = (numVolumes: any) => {
    if (!fieldValue) return;
    const volumeCount = Number(numVolumes) > maxVolumeCount ? maxVolumeCount : Number(numVolumes);
    setValue(UPDATE_FIELD, { ...fieldValue, numVolumes: volumeCount });
  };

  return (
    <Controller
      name={UPDATE_FIELD}
      control={control}
      rules={{
        required: t('universeForm.validation.required', {
          field: t('universeForm.instanceConfig.instanceType')
        }) as string
      }}
      render={() => {
        return (
          <Box display="flex" flexDirection="column">
            <Box display="flex">
              <Box>
                <YBLabel>{t('provisionedThroughputPerPod')}</YBLabel>
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
                />
              </Box>
              <Box ml={2} display="flex" alignItems="center" className={classes.unitLabelField}>
                {t('k8VolumeSizeUnit')}
              </Box>
            </Box>
          </Box>
        );
      }}
    />
  );
};
