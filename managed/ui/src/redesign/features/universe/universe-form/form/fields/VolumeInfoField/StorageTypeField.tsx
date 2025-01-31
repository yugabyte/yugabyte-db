import { FC, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { Box, Grid, MenuItem, makeStyles } from '@material-ui/core';
import { YBLabel, YBSelect} from '../../../../../../components';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import { api, QUERY_KEY } from '../../../utils/api';
import {
  getThroughputByStorageType,
  getStorageTypeOptions,
  getIopsByStorageType,
  useVolumeControls
} from './VolumeInfoFieldHelper';
import { StorageType, UniverseFormData, CloudType } from '../../../utils/dto';
import { IsOsPatchingEnabled } from '../../../../../../../components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';

import {
  DEVICE_INFO_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  PROVIDER_FIELD,
  INSTANCE_TYPE_FIELD,
  CPU_ARCHITECTURE_FIELD
} from '../../../utils/constants';

const useStyles = makeStyles((theme) => ({
  storageTypeLabelField: {
    minWidth: theme.spacing(21.25)
  },
  storageTypeSelectField: {
    maxWidth: theme.spacing(35.25),
    minWidth: theme.spacing(30)
  }
}));

interface StorageTypeFieldProps {
  isViewMode: boolean;
  isEditMode: boolean;
}

export const StorageTypeField: FC<StorageTypeFieldProps> = ({ isViewMode, isEditMode }) => {
  const { t } = useTranslation();
  const classes = useStyles();

  const { universeConfigureTemplate } = useContext(UniverseFormContext)[0];
  const updateOptions = universeConfigureTemplate?.updateOptions;

  // watchers
  const fieldValue = useWatch({ name: DEVICE_INFO_FIELD });
  const masterFieldValue = useWatch({ name: MASTER_DEVICE_INFO_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });
  const instanceType = useWatch({ name: INSTANCE_TYPE_FIELD });
  const cpuArch = useWatch({ name: CPU_ARCHITECTURE_FIELD });
  const { setValue } = useFormContext<UniverseFormData>();
  const { disableStorageType } = useVolumeControls(isEditMode, updateOptions);

  //field actions
  const onStorageTypeChanged = (storageType: StorageType) => {
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
      fieldValue.storageType === StorageType.Persistent &&
      masterFieldValue.storageType === StorageType.Scratch
    ) {
      setValue(MASTER_DEVICE_INFO_FIELD, {
        ...masterFieldValue,
        throughput,
        diskIops,
        storageType
      });
    }
    if (
      fieldValue.storageType === StorageType.Scratch &&
      masterFieldValue.storageType === StorageType.Persistent
    ) {
      setValue(DEVICE_INFO_FIELD, {
        ...masterFieldValue,
        throughput,
        diskIops,
        storageType
      });
    }
  }, [fieldValue.storageType, masterFieldValue?.storageType]);

  const isOsPatchingEnabled = IsOsPatchingEnabled();

  //get instance details
  const { data: instanceTypes } = useQuery(
    [QUERY_KEY.getInstanceTypes, provider?.uuid, isOsPatchingEnabled ? cpuArch : null],
    () => api.getInstanceTypes(provider?.uuid, [], isOsPatchingEnabled ? cpuArch : null),
    { enabled: !!provider?.uuid }
  );

  const instance = instanceTypes?.find((item) => item.instanceTypeCode === instanceType);
  if (!instance) return null;

  const storageType = fieldValue.storageType;

  const renderStorageType = () => {
    if ([CloudType.gcp, CloudType.azu].includes(provider?.code))
      return (
        <Box display="flex">
          <YBLabel
            dataTestId="VolumeInfoFieldDedicated-Common-StorageTypeLabel"
            className={classes.storageTypeLabelField}
          >
            {provider?.code === CloudType.aws
              ? t('universeForm.instanceConfig.ebs')
              : t('universeForm.instanceConfig.ssd')}
          </YBLabel>
          <Box flex={1}>
            <YBSelect
              className={classes.storageTypeSelectField}
              fullWidth
              disabled={isViewMode || disableStorageType}
              value={storageType}
              inputProps={{
                min: 1,
                'data-testid': 'VolumeInfoFieldDedicated-Common-StorageTypeSelect'
              }}
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

  return (
    <Box>
      <Grid container spacing={2}>
        <Grid item lg={6} sm={12}>
          <Box mt={2}>{renderStorageType()}</Box>
        </Grid>
      </Grid>
    </Box>
  );
};
