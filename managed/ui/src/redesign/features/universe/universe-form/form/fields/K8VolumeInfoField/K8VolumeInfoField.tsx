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
  isDedicatedMasterField: boolean;
  disableVolumeSize: boolean;
  disableNumVolumes: boolean;
  isEditMode: boolean;
  maxVolumeCount: number;
}

export const K8VolumeInfoField = ({
  isDedicatedMasterField,
  disableVolumeSize,
  disableNumVolumes,
  isEditMode,
  maxVolumeCount
}: K8VolumeInfoFieldProps): ReactElement => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const classes = useStyles();
  const { t } = useTranslation();
  const nodeTypeTag = isDedicatedMasterField ? NodeType.Master : NodeType.TServer;

  // watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const fieldValue = isDedicatedMasterField
    ? useWatch({ name: MASTER_DEVICE_INFO_FIELD })
    : useWatch({ name: DEVICE_INFO_FIELD });
  const UPDATE_FIELD = isDedicatedMasterField ? MASTER_DEVICE_INFO_FIELD : DEVICE_INFO_FIELD;
  // To set value based on master or tserver field in dedicated mode
  const INSTANCE_TYPE_UPDATE_FIELD = isDedicatedMasterField
    ? MASTER_INSTANCE_TYPE_FIELD
    : INSTANCE_TYPE_FIELD;
  const convertToString = (str: string) => str?.toString() ?? '';

  //fetch run time configs
  const {
    data: providerRuntimeConfigs,
    refetch: providerConfigsRefetch
  } = useQuery(QUERY_KEY.fetchProviderRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, provider?.uuid)
  );

  useEffect(() => {
    const getProviderRuntimeConfigs = async () => {
      await providerConfigsRefetch();
      let deviceInfo = getK8DeviceInfo(providerRuntimeConfigs);

      if (fieldValue && deviceInfo && isEditMode) {
        deviceInfo.volumeSize = fieldValue.volumeSize;
        deviceInfo.numVolumes = fieldValue.numVolumes;
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

  return (
    <Controller
      name={UPDATE_FIELD}
      control={control}
      rules={{
        required: t('universeForm.validation.required', {
          field: t('universeForm.instanceConfig.instanceType')
        }) as string
      }}
      render={({}) => {
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
                        disabled={disableNumVolumes}
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
              </Box>
            </Grid>
          </Grid>
        );
      }}
    />
  );
};
