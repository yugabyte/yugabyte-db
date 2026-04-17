import { ReactElement, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { useQuery } from 'react-query';
import { Box, Grid, makeStyles } from '@material-ui/core';
import { YBLabel, YBInput } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import { getK8DeviceInfo } from './K8VolumeInfoFieldHelper';
import { UniverseFormData } from '../../../utils/dto';
import { NodeType } from '../../../../../../utils/dtos';
import {
  DEVICE_INFO_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  PROVIDER_FIELD
} from '../../../utils/constants';

const useStyles = makeStyles((theme) => ({
  volumeInfoTextField: {
    width: theme.spacing(15.5)
  },
  unitLabelField: {
    marginLeft: theme.spacing(2),
    alignSelf: 'center'
  }
}));

interface K8VolumeInfoFieldProps {
  isMaster: boolean;
  disableVolumeSize: boolean;
  isEditMode: boolean;
  maxVolumeCount: number;
  k8sOverrideEnabled?: boolean;
}

export const K8VolumeInfoField = ({
  isMaster,
  disableVolumeSize,
  isEditMode,
  maxVolumeCount,
  k8sOverrideEnabled = false
}: K8VolumeInfoFieldProps): ReactElement => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const classes = useStyles();
  const { t } = useTranslation();
  const nodeTypeTag = isMaster ? NodeType.Master : NodeType.TServer;

  // watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const tserverDeviceInfo = useWatch({ name: DEVICE_INFO_FIELD });
  const masterDeviceInfo = useWatch({ name: MASTER_DEVICE_INFO_FIELD });
  const fieldValue = isMaster ? masterDeviceInfo : tserverDeviceInfo;
  const UPDATE_FIELD = isMaster ? MASTER_DEVICE_INFO_FIELD : DEVICE_INFO_FIELD;
  // To set value based on master or tserver field in dedicated mode
  const INSTANCE_TYPE_UPDATE_FIELD = isMaster ? MASTER_INSTANCE_TYPE_FIELD : INSTANCE_TYPE_FIELD;
  const convertToString = (str: string) => str?.toString() ?? '';

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

      if (fieldValue && deviceInfo && isEditMode) {
        deviceInfo.volumeSize = fieldValue.volumeSize;
        deviceInfo.numVolumes = fieldValue.numVolumes;
        deviceInfo.storageClass = fieldValue.storageClass;
      }
      setValue(UPDATE_FIELD, deviceInfo);
    };
    getProviderRuntimeConfigs();
    setValue(INSTANCE_TYPE_UPDATE_FIELD, null);
  }, []);

  const onVolumeSizeChanged = (value: any) => {
    setValue(UPDATE_FIELD, { ...fieldValue, volumeSize: Number(value) });
  };
  const onNumVolumesChanged = (numVolumes: any) => {
    const volumeCount = Number(numVolumes) > maxVolumeCount ? maxVolumeCount : Number(numVolumes);
    setValue(UPDATE_FIELD, { ...fieldValue, numVolumes: volumeCount });
  };
  const onStorageClassChanged = (value: string) => {
    setValue(UPDATE_FIELD, { ...fieldValue, storageClass: value as any });
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
          <Grid container spacing={2}>
            <Grid item lg={6}>
              <Box mt={2}>
                <Box display="flex">
                  <Box display="flex">
                    <YBLabel dataTestId="K8VolumeInfoField-Label">
                      {t('universeForm.instanceConfig.volumeInfo')}
                    </YBLabel>
                  </Box>

                  <Box display="flex">
                    <Box flex={1} className={classes.volumeInfoTextField}>
                      <YBInput
                        type="number"
                        fullWidth
                        disabled={isEditMode && !k8sOverrideEnabled}
                        inputProps={{
                          min: 1,
                          'data-testid': `K8VolumeInfoField-${nodeTypeTag}-VolumeInput`
                        }}
                        value={convertToString(fieldValue?.numVolumes)}
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
                        disabled={disableVolumeSize}
                        inputProps={{
                          min: 1,
                          'data-testid': `K8VolumeInfoField-${nodeTypeTag}-VolumeSizeInput`
                        }}
                        value={convertToString(fieldValue?.volumeSize)}
                        onChange={(event) => onVolumeSizeChanged(event.target.value)}
                        inputMode="numeric"
                      />
                    </Box>
                    <span className={classes.unitLabelField}>
                      {t('universeForm.instanceConfig.k8VolumeSizeUnit')}
                    </span>
                  </Box>
                </Box>
                {k8sOverrideEnabled && (
                  <Box display="flex" mt={2}>
                    <YBLabel dataTestId={`K8VolumeInfoField-${nodeTypeTag}-StorageClassLabel`}>
                      {t('universeForm.instanceConfig.storageClass')}
                    </YBLabel>
                    <Box flex={1} className={classes.volumeInfoTextField} width={'100%'}>
                      <YBInput
                        fullWidth
                        inputProps={{
                          'data-testid': `K8VolumeInfoField-${nodeTypeTag}-StorageClassInput`
                        }}
                        value={fieldValue?.storageClass ?? 'standard'}
                        onChange={(event) => onStorageClassChanged(event.target.value)}
                      />
                    </Box>
                  </Box>
                )}
              </Box>
            </Grid>
          </Grid>
        );
      }}
    />
  );
};
