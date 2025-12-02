import { FC, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import clsx from 'clsx';
import { Box, Grid, MenuItem, makeStyles } from '@material-ui/core';
import {
  YBHelper,
  YBHelperVariants,
  YBInput,
  YBLabel,
  YBSelect
} from '../../../../../../components';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import { IsOsPatchingEnabled } from '../../../../../../../components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import {
  getThroughputByStorageType,
  getStorageTypeOptions,
  getIopsByStorageType,
  useVolumeControls,
  getMaxDiskIops,
  getMinDiskIops,
  getThroughputByIops
} from './VolumeInfoFieldHelper';
import { api, QUERY_KEY } from '../../../utils/api';
import { StorageType, UniverseFormData, CloudType } from '../../../utils/dto';
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
  },
  warningStorageLabelField: {
    marginTop: theme.spacing(2),
    alignItems: 'flex-start'
  },
  unitLabelField: {
    marginLeft: theme.spacing(2),
    alignSelf: 'center'
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

  //fetch run time configs
  const {
    data: providerRuntimeConfigs,
    refetch: providerConfigsRefetch
  } = useQuery(QUERY_KEY.fetchProviderRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, provider?.uuid)
  );

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
      masterFieldValue?.storageType === StorageType.Scratch
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
      masterFieldValue?.storageType === StorageType.Persistent
    ) {
      setValue(DEVICE_INFO_FIELD, {
        ...masterFieldValue,
        throughput,
        diskIops,
        storageType
      });
    }
  }, [
    fieldValue.storageType,
    masterFieldValue?.storageType,
    fieldValue.throughput,
    masterFieldValue?.throughput
  ]);

  const convertToString = (str: string) => str?.toString() ?? '';

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

  const renderStorageHelperText = (isPremiumV2Storage: boolean, isHyperdisk: boolean) => {
    if (isHyperdisk) {
      return (
        <YBHelper variant={YBHelperVariants.warning}>
          {t('universeForm.instanceConfig.hyperdiskStorage')}
        </YBHelper>
      );
    }
    if (isPremiumV2Storage) {
      return (
        <YBHelper variant={YBHelperVariants.warning}>
          {t('universeForm.instanceConfig.premiumv2Storage')}
        </YBHelper>
      );
    }
    return null;
  };

  const renderStorageType = () => {
    const isPremiumV2Storage = fieldValue.storageType === StorageType.PremiumV2_LRS;
    const isHyperdisk =
      fieldValue.storageType === StorageType.Hyperdisk_Balanced ||
      fieldValue.storageType === StorageType.Hyperdisk_Extreme;
    if ([CloudType.gcp, CloudType.azu].includes(provider?.code))
      return (
        <Box display="flex">
          <YBLabel
            dataTestId="StorageTypeField-Common-StorageTypeLabel"
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
              fullWidth
              disabled={isViewMode || disableStorageType}
              value={storageType}
              inputProps={{
                min: 1,
                'data-testid': 'StorageTypeField-Common-StorageTypeSelect'
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
            <YBLabel dataTestId="StorageTypeField-DiskIopsLabel">
              {t('universeForm.instanceConfig.provisionedIops')}
            </YBLabel>
            <Box flex={1}>
              <YBInput
                type="number"
                fullWidth
                disabled={isViewMode}
                inputProps={{ min: 1, 'data-testid': `StorageTypeField-DiskIopsInput` }}
                value={convertToString(fieldValue.diskIops)}
                onChange={(event) => onDiskIopsChanged(event.target.value)}
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
    ) {
      return null;
    }

    return (
      <Grid container spacing={2}>
        <Grid item lg={6}>
          <Box display="flex" mt={1}>
            <YBLabel dataTestId="StorageTypeField-ThroughputLabel">
              {t('universeForm.instanceConfig.provisionedThroughput')}
            </YBLabel>
            <Box flex={1}>
              <YBInput
                type="number"
                fullWidth
                disabled={isViewMode}
                inputProps={{ min: 1, 'data-testid': `StorageTypeField-ThroughputInput` }}
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

  const onDiskIopsChanged = (value: any) => {
    const { storageType, volumeSize } = fieldValue;
    const maxDiskIops = getMaxDiskIops(storageType, volumeSize);
    const minDiskIops = getMinDiskIops(storageType, volumeSize);
    const diskIops = Math.max(minDiskIops, Math.min(maxDiskIops, Number(value)));
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, diskIops });
    setValue(MASTER_DEVICE_INFO_FIELD, {
      ...masterFieldValue,
      diskIops
    });
  };

  const onThroughputChange = (value: any) => {
    const { storageType, diskIops } = fieldValue;
    const throughput = getThroughputByIops(Number(value), diskIops, storageType);
    setValue(DEVICE_INFO_FIELD, { ...fieldValue, throughput });
    setValue(MASTER_DEVICE_INFO_FIELD, {
      ...masterFieldValue,
      throughput
    });
  };

  return (
    <Box display="flex" width="100%" flexDirection="column">
      <Box>
        <Grid item lg={6} sm={12}>
          <Box mt={2}>{renderStorageType()}</Box>
        </Grid>
      </Box>
      <Box>
        {fieldValue.storageType && (
          <Box mt={2}>
            {renderDiskIops()}
            {renderThroughput()}
          </Box>
        )}
      </Box>
    </Box>
  );
};
