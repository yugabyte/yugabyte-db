import { useState } from 'react';
import { useUpdateEffect } from 'react-use';
import { useWatch } from 'react-hook-form';
import {
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  PLACEMENTS_FIELD,
  PROVIDER_FIELD,
  TOTAL_NODES_FIELD
} from '../../../utils/constants';
import {
  CloudType,
  DeviceInfo,
  InstanceType,
  RunTimeConfigEntry,
  StorageType
} from '../../../utils/dto';
import { isEphemeralAwsStorageInstance } from '../InstanceTypeField/InstanceTypeFieldHelper';
import { RuntimeConfigKey } from '../../../../../../helpers/constants';

export const IO1_DEFAULT_DISK_IOPS = 1000;
export const IO1_MAX_DISK_IOPS = 64000;

export const IO2_DEFAULT_DISK_IOPS = 1000;
export const IO2_MAX_DISK_IOPS = 256000;

export const GP3_DEFAULT_DISK_IOPS = 3000;
export const GP3_MAX_IOPS = 16000;
export const GP3_DEFAULT_DISK_THROUGHPUT = 125;
export const GP3_MAX_THROUGHPUT = 1000;
export const GP3_IOPS_TO_MAX_DISK_THROUGHPUT = 4;

export const UltraSSD_DEFAULT_DISK_IOPS = 3000;
export const UltraSSD_DEFAULT_DISK_THROUGHPUT = 125;
export const UltraSSD_MIN_DISK_IOPS = 100;
export const UltraSSD_DISK_IOPS_MAX_PER_GB = 300;
export const UltraSSD_IOPS_TO_MAX_DISK_THROUGHPUT = 4;
export const UltraSSD_DISK_THROUGHPUT_CAP = 2500;

export const PremiumV2_LRS_DEFAULT_DISK_IOPS = 3000;
export const PremiumV2_LRS_DEFAULT_DISK_THROUGHPUT = 125;
export const PremiumV2_LRS_MIN_DISK_IOPS = 3000;
export const PremiumV2_LRS_DISK_IOPS_MAX_PER_GB = 500;
export const PremiumV2_LRS_IOPS_TO_MAX_DISK_THROUGHPUT = 4;
export const PremiumV2_LRS_DISK_THROUGHPUT_CAP = 2500;

export const HB_DEFAULT_DISK_IOPS = 3600;
export const HB_DEFAULT_DISK_THROUGHPUT = 290;
export const HB_MIN_DISK_IOPS = 3000;
export const HB_DISK_IOPS_MAX_PER_GB = 500;
export const HB_MAX_DISK_IOPS = 160000;
export const HB_IOPS_TO_MAX_DISK_THROUGHPUT = 4;
export const HB_DISK_THROUGHPUT_CAP = 2400;

export const HE_DEFAULT_DISK_IOPS = 25000;
export const HE_MIN_DISK_IOPS = 2;
export const HE_MAX_DISK_IOPS = 350000;
export const HE_DISK_IOPS_MAX_PER_GB = 1000;

export interface StorageTypeOption {
  value: StorageType;
  label: string;
}

export const DEFAULT_STORAGE_TYPES = {
  [CloudType.aws]: StorageType.GP3,
  [CloudType.gcp]: StorageType.Persistent,
  [CloudType.azu]: StorageType.Premium_LRS
};

export const AWS_STORAGE_TYPE_OPTIONS: StorageTypeOption[] = [
  { value: StorageType.IO1, label: 'IO1' },
  { value: StorageType.IO2, label: 'IO2' },
  { value: StorageType.GP2, label: 'GP2' },
  { value: StorageType.GP3, label: 'GP3' }
];

export const GCP_STORAGE_TYPE_OPTIONS: StorageTypeOption[] = [
  { value: StorageType.Persistent, label: 'Persistent' },
  { value: StorageType.Scratch, label: 'Local Scratch' },
  { value: StorageType.Hyperdisk_Balanced, label: 'Hyperdisk Balanced' },
  { value: StorageType.Hyperdisk_Extreme, label: 'Hyperdisk Extreme' }
];

export const AZURE_STORAGE_TYPE_OPTIONS: StorageTypeOption[] = [
  { value: StorageType.StandardSSD_LRS, label: 'Standard' },
  { value: StorageType.Premium_LRS, label: 'Premium' },
  { value: StorageType.PremiumV2_LRS, label: 'PremiumV2' },
  { value: StorageType.UltraSSD_LRS, label: 'Ultra' }
];

export const getMinDiskIops = (storageType: StorageType, volumeSize: number) => {
  switch (storageType) {
    case StorageType.UltraSSD_LRS:
      return Math.max(UltraSSD_MIN_DISK_IOPS, volumeSize);
    case StorageType.PremiumV2_LRS:
      return Math.max(PremiumV2_LRS_MIN_DISK_IOPS, volumeSize);
    case StorageType.Hyperdisk_Balanced:
      return HB_MIN_DISK_IOPS;
    case StorageType.Hyperdisk_Extreme:
      return HE_MIN_DISK_IOPS;
    default:
      return 0;
  }
};

export const getMaxDiskIops = (storageType: StorageType, volumeSize: number) => {
  switch (storageType) {
    case StorageType.IO1:
      return IO1_MAX_DISK_IOPS;
    case StorageType.IO2:
      return IO2_MAX_DISK_IOPS;
    case StorageType.UltraSSD_LRS:
      return volumeSize * UltraSSD_DISK_IOPS_MAX_PER_GB;
    case StorageType.PremiumV2_LRS:
      return volumeSize * PremiumV2_LRS_DISK_IOPS_MAX_PER_GB;
    case StorageType.Hyperdisk_Balanced:
      return Math.min(HB_MAX_DISK_IOPS, HB_DISK_IOPS_MAX_PER_GB * volumeSize);
    case StorageType.Hyperdisk_Extreme:
      return Math.min(HE_MAX_DISK_IOPS, HE_DISK_IOPS_MAX_PER_GB * volumeSize);
    default:
      return GP3_MAX_IOPS;
  }
};

export const getStorageTypeOptions = (
  providerCode?: CloudType,
  providerRuntimeConfigs?: any
): StorageTypeOption[] => {
  const showPremiumV2Option =
    providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.AZURE_PREMIUM_V2_STORAGE_TYPE
    )?.value === 'true';
  const filteredAzureStorageTypes = showPremiumV2Option
    ? AZURE_STORAGE_TYPE_OPTIONS
    : AZURE_STORAGE_TYPE_OPTIONS.filter((storageType) => {
        return storageType.value !== StorageType.PremiumV2_LRS;
      });
  const showHyperdisksOption =
    providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.HYPERDISKS_STORAGE_TYPE
    )?.value === 'true';
  const filteredGcpStorageTypes = showHyperdisksOption
    ? GCP_STORAGE_TYPE_OPTIONS
    : GCP_STORAGE_TYPE_OPTIONS.filter((storageType) => {
        return (
          storageType.value !== StorageType.Hyperdisk_Balanced &&
          storageType.value !== StorageType.Hyperdisk_Extreme
        );
      });
  switch (providerCode) {
    case CloudType.aws:
      return AWS_STORAGE_TYPE_OPTIONS;
    case CloudType.gcp:
      return filteredGcpStorageTypes;
    case CloudType.azu:
      return filteredAzureStorageTypes;
    default:
      return [];
  }
};

export const getIopsByStorageType = (storageType: StorageType) => {
  if (storageType === StorageType.IO1) {
    return IO1_DEFAULT_DISK_IOPS;
  }else if (storageType === StorageType.IO2) {
      return IO2_DEFAULT_DISK_IOPS;
  } else if (storageType === StorageType.GP3) {
    return GP3_DEFAULT_DISK_IOPS;
  } else if (storageType === StorageType.UltraSSD_LRS) {
    return UltraSSD_DEFAULT_DISK_IOPS;
  } else if (storageType === StorageType.PremiumV2_LRS) {
    return PremiumV2_LRS_DEFAULT_DISK_IOPS;
  } else if (storageType === StorageType.Hyperdisk_Balanced) {
    return HB_DEFAULT_DISK_IOPS;
  } else if (storageType === StorageType.Hyperdisk_Extreme) {
    return HE_DEFAULT_DISK_IOPS;
  }
  return null;
};

export const getThroughputByStorageType = (storageType: StorageType) => {
  if (storageType === StorageType.GP3) {
    return GP3_DEFAULT_DISK_THROUGHPUT;
  } else if (storageType === StorageType.UltraSSD_LRS) {
    return UltraSSD_DEFAULT_DISK_THROUGHPUT;
  } else if (storageType === StorageType.PremiumV2_LRS) {
    return PremiumV2_LRS_DEFAULT_DISK_THROUGHPUT;
  } else if (storageType === StorageType.Hyperdisk_Balanced) {
    return HB_DEFAULT_DISK_THROUGHPUT;
  }
  return null;
};

export const getThroughputByIops = (
  currentThroughput: number,
  diskIops: number,
  storageType: StorageType
) => {
  if (storageType === StorageType.GP3) {
    if (
      (diskIops > GP3_DEFAULT_DISK_IOPS || currentThroughput > GP3_DEFAULT_DISK_THROUGHPUT) &&
      diskIops / currentThroughput < GP3_IOPS_TO_MAX_DISK_THROUGHPUT
    ) {
      return Math.min(
        GP3_MAX_THROUGHPUT,
        Math.max(diskIops / GP3_IOPS_TO_MAX_DISK_THROUGHPUT, GP3_DEFAULT_DISK_THROUGHPUT)
      );
    }
  } else if (storageType === StorageType.UltraSSD_LRS) {
    const maxThroughput = Math.min(
      diskIops / UltraSSD_IOPS_TO_MAX_DISK_THROUGHPUT,
      UltraSSD_DISK_THROUGHPUT_CAP
    );
    return Math.max(0, Math.min(maxThroughput, currentThroughput));
  } else if (storageType === StorageType.PremiumV2_LRS) {
    const maxThroughput = Math.min(
      diskIops / PremiumV2_LRS_IOPS_TO_MAX_DISK_THROUGHPUT,
      PremiumV2_LRS_DISK_THROUGHPUT_CAP
    );
    return Math.max(0, Math.min(maxThroughput, currentThroughput));
  } else if (storageType === StorageType.Hyperdisk_Balanced) {
    const maxThroughput = Math.min(
      diskIops / HB_IOPS_TO_MAX_DISK_THROUGHPUT,
      HB_DISK_THROUGHPUT_CAP
    );
    return Math.max(0, Math.min(maxThroughput, currentThroughput));
  }

  return currentThroughput;
};

const getVolumeSize = (instance: InstanceType, providerRuntimeConfigs: any) => {
  let volumeSize = null;

  if (instance.providerCode === CloudType.aws) {
    volumeSize = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.AWS_DEFAULT_VOLUME_SIZE
    )?.value;
  } else if (instance.providerCode === CloudType.gcp) {
    volumeSize = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.GCP_DEFAULT_VOLUME_SIZE
    )?.value;
  } else if (instance.providerCode === CloudType.kubernetes) {
    volumeSize = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.KUBERNETES_DEFAULT_VOLUME_SIZE
    )?.value;
  } else if (instance.providerCode === CloudType.azu) {
    volumeSize = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.AZURE_DEFAULT_VOLUME_SIZE
    )?.value;
  }
  return volumeSize;
};

const getStorageType = (instance: InstanceType, providerRuntimeConfigs: any) => {
  let storageType = null;
  if (isEphemeralAwsStorageInstance(instance))
    //aws ephemeral storage
    return storageType;

  if (instance.providerCode === CloudType.aws) {
    storageType = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.AWS_DEFAULT_STORAGE_TYPE
    )?.value;
  } else if (instance.providerCode === CloudType.gcp) {
    storageType = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.GCP_DEFAULT_STORAGE_TYPE
    )?.value;
  } else if (instance.providerCode === CloudType.azu) {
    storageType = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.AZURE_DEFAULT_STORAGE_TYPE
    )?.value;
  }
  return storageType;
};

export const getDeviceInfoFromInstance = (
  instance: InstanceType,
  providerRuntimeConfigs: any,
  isEditMode: boolean,
  deviceInfo: DeviceInfo
): DeviceInfo | null => {
  if (!instance.instanceTypeDetails.volumeDetailsList.length) return null;

  const { volumeDetailsList } = instance.instanceTypeDetails;
  const volumeSize = volumeDetailsList[0].volumeSizeGB;
  const defaultInstanceVolumeSize = isEphemeralAwsStorageInstance(instance)
    ? volumeSize
    : getVolumeSize(instance, providerRuntimeConfigs);
  const storageType = isEditMode
    ? deviceInfo?.storageType
    : getStorageType(instance, providerRuntimeConfigs);
  // Disk IOPS does not exist for all storage types
  const diskIops =
    isEditMode && deviceInfo?.diskIops ? deviceInfo?.diskIops : getIopsByStorageType(storageType);
  // Throughput does not exist for all storage types
  const throughput =
    isEditMode && deviceInfo?.throughput
      ? deviceInfo?.throughput
      : getThroughputByStorageType(storageType);

  return {
    numVolumes: volumeDetailsList.length,
    volumeSize: defaultInstanceVolumeSize ?? volumeSize,
    storageClass: 'standard',
    storageType,
    mountPoints:
      instance.providerCode === CloudType.onprem
        ? volumeDetailsList.flatMap((item) => item.mountPath).join(',')
        : null,
    diskIops: diskIops,
    throughput: throughput,
    cloudVolumeEncryption: deviceInfo?.cloudVolumeEncryption
  };
};

export const useVolumeControls = (isEditMode: boolean, updateOptions: string[]) => {
  const [numVolumesDisable, setNumVolumesDisable] = useState(false);
  const [volumeSizeDisable, setVolumeSizeDisable] = useState(false);
  const [userTagsDisable, setUserTagsDisable] = useState(false);
  const [disableIops, setDisableIops] = useState(false);
  const [disableThroughput, setDisableThroughput] = useState(false);
  const [disableStorageType, setDisableStorageType] = useState(false);
  const [minVolumeSize, setMinVolumeSize] = useState(1);

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const totalNodes = useWatch({ name: TOTAL_NODES_FIELD });
  const placements = useWatch({ name: PLACEMENTS_FIELD });
  const instanceType = useWatch({ name: INSTANCE_TYPE_FIELD });
  const deviceInfo = useWatch({ name: DEVICE_INFO_FIELD });

  useUpdateEffect(() => {
    if (isEditMode && provider.code !== CloudType.kubernetes) {
      setNumVolumesDisable(false);
      setVolumeSizeDisable(false);
      setUserTagsDisable(false);
      setDisableIops(false);
      setDisableThroughput(false);
      setDisableStorageType(false);
    }
  }, [totalNodes, placements, instanceType, deviceInfo?.volumeSize]);

  return {
    numVolumesDisable,
    volumeSizeDisable,
    userTagsDisable,
    minVolumeSize,
    disableIops,
    disableThroughput,
    disableStorageType
  };
};
