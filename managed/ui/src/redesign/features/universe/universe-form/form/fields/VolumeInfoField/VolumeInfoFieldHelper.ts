import { CloudType, DeviceInfo, InstanceType, StorageType } from '../../../utils/dto';
import { isEphemeralAwsStorageInstance } from '../InstanceTypeField/InstanceTypeFieldHelper';

export const IO1_DEFAULT_DISK_IOPS = 1000;
export const IO1_MAX_DISK_IOPS = 64000;

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
  { value: StorageType.GP2, label: 'GP2' },
  { value: StorageType.GP3, label: 'GP3' }
];

export const GCP_STORAGE_TYPE_OPTIONS: StorageTypeOption[] = [
  { value: StorageType.Persistent, label: 'Persistent' },
  { value: StorageType.Scratch, label: 'Local Scratch' }
];

export const AZURE_STORAGE_TYPE_OPTIONS: StorageTypeOption[] = [
  { value: StorageType.StandardSSD_LRS, label: 'Standard' },
  { value: StorageType.Premium_LRS, label: 'Premium' },
  { value: StorageType.UltraSSD_LRS, label: 'Ultra' }
];

export const getMinDiskIops = (storageType: StorageType, volumeSize: number) => {
  return storageType === StorageType.UltraSSD_LRS
    ? Math.max(UltraSSD_MIN_DISK_IOPS, volumeSize)
    : 0;
};

export const getMaxDiskIops = (storageType: StorageType, volumeSize: number) => {
  switch (storageType) {
    case StorageType.IO1:
      return IO1_MAX_DISK_IOPS;
    case StorageType.UltraSSD_LRS:
      return volumeSize * UltraSSD_DISK_IOPS_MAX_PER_GB;
    default:
      return GP3_MAX_IOPS;
  }
};

export const getStorageTypeOptions = (providerCode?: CloudType): StorageTypeOption[] => {
  switch (providerCode) {
    case CloudType.aws:
      return AWS_STORAGE_TYPE_OPTIONS;
    case CloudType.gcp:
      return GCP_STORAGE_TYPE_OPTIONS;
    case CloudType.azu:
      return AZURE_STORAGE_TYPE_OPTIONS;
    default:
      return [];
  }
};

export const getIopsByStorageType = (storageType: StorageType) => {
  if (storageType === StorageType.IO1) {
    return IO1_DEFAULT_DISK_IOPS;
  } else if (storageType === StorageType.GP3) {
    return GP3_DEFAULT_DISK_IOPS;
  } else if (storageType === StorageType.UltraSSD_LRS) {
    return UltraSSD_DEFAULT_DISK_IOPS;
  }
  return null;
};

export const getThroughputByStorageType = (storageType: StorageType) => {
  if (storageType === StorageType.GP3) {
    return GP3_DEFAULT_DISK_THROUGHPUT;
  } else if (storageType === StorageType.UltraSSD_LRS) {
    return UltraSSD_DEFAULT_DISK_THROUGHPUT;
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
  }

  return currentThroughput;
};

const getStorageType = (instance: InstanceType) => {
  if (isEphemeralAwsStorageInstance(instance))
    //aws ephemeral storage
    return null;
  return DEFAULT_STORAGE_TYPES[instance.providerCode] ?? null;
};

export const getDeviceInfoFromInstance = (instance: InstanceType): DeviceInfo | null => {
  if (!instance.instanceTypeDetails.volumeDetailsList.length) return null;

  const { volumeDetailsList } = instance.instanceTypeDetails;
  const storageType = getStorageType(instance);

  return {
    numVolumes: volumeDetailsList.length,
    volumeSize: volumeDetailsList[0].volumeSizeGB,
    storageClass: 'standard',
    storageType,
    mountPoints:
      instance.providerCode === CloudType.onprem
        ? volumeDetailsList.flatMap((item) => item.mountPath).join(',')
        : null,
    diskIops: getIopsByStorageType(storageType),
    throughput: getThroughputByStorageType(storageType)
  };
};
