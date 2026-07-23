import { useState } from 'react';
import { useUpdateEffect } from 'react-use';
import { useWatch } from 'react-hook-form';
import {
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  PLACEMENTS_FIELD,
  PROVIDER_FIELD,
  TOTAL_NODES_FIELD
} from '@app/redesign/features/universe/universe-form/utils/constants';
import {
  CloudType,
  DeviceInfo,
  InstanceType,
  RunTimeConfigEntry,
  StorageType
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { isEphemeralAwsStorageInstance } from '@app/redesign/features-v2/universe/create-universe/fields/instance-type/InstanceTypeFieldHelper';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';

// iops / throughput limits (same as backend StorageType ranges)
export const IO1_DEFAULT_DISK_IOPS = 1000;
export const IO1_MIN_DISK_IOPS = 100;
export const IO1_MAX_DISK_IOPS = 64000;
export const IO2_DEFAULT_DISK_IOPS = 1000;
export const IO2_MIN_DISK_IOPS = 100;
export const IO2_MAX_DISK_IOPS = 256000;

export const GP3_DEFAULT_DISK_IOPS = 3000;
export const GP3_MIN_DISK_IOPS = 3000;
export const GP3_MAX_IOPS = 80000;
export const GP3_DEFAULT_DISK_THROUGHPUT = 125;
export const GP3_MIN_THROUGHPUT = 125;
export const GP3_MAX_THROUGHPUT = 2000;
export const GP3_IOPS_TO_MAX_DISK_THROUGHPUT = 4;

export const UltraSSD_DEFAULT_DISK_IOPS = 3000;
export const UltraSSD_DEFAULT_DISK_THROUGHPUT = 125;
export const UltraSSD_MIN_DISK_IOPS = 100;
export const UltraSSD_MAX_DISK_IOPS = 160000;
export const UltraSSD_MIN_THROUGHPUT = 1;
export const UltraSSD_DISK_IOPS_MAX_PER_GB = 300;
export const UltraSSD_IOPS_TO_MAX_DISK_THROUGHPUT = 4;
export const UltraSSD_DISK_THROUGHPUT_CAP = 3814;

export const PremiumV2_LRS_DEFAULT_DISK_IOPS = 3000;
export const PremiumV2_LRS_DEFAULT_DISK_THROUGHPUT = 125;
export const PremiumV2_LRS_MIN_DISK_IOPS = 3000;
export const PremiumV2_LRS_MAX_DISK_IOPS = 80000;
export const PremiumV2_LRS_MIN_THROUGHPUT = 1;
export const PremiumV2_LRS_DISK_IOPS_MAX_PER_GB = 500;
export const PremiumV2_LRS_IOPS_TO_MAX_DISK_THROUGHPUT = 4;
export const PremiumV2_LRS_DISK_THROUGHPUT_CAP = 1200;

export const HB_DEFAULT_DISK_IOPS = 3600;
export const HB_DEFAULT_DISK_THROUGHPUT = 290;
export const HB_MIN_DISK_IOPS = 3000;
export const HB_DISK_IOPS_MAX_PER_GB = 500;
export const HB_MAX_DISK_IOPS = 160000;
export const HB_MIN_THROUGHPUT = 250;
export const HB_IOPS_TO_MAX_DISK_THROUGHPUT = 4;
export const HB_DISK_THROUGHPUT_CAP = 2400;

export const HE_DEFAULT_DISK_IOPS = 25000;
export const HE_MIN_DISK_IOPS = 2;
export const HE_MAX_DISK_IOPS = 350000;
export const HE_DISK_IOPS_MAX_PER_GB = 1000;

export type NumericRange = { min: number; max: number };

// same ranges as PublicCloudConstants.StorageType
export const getDiskIopsRange = (
  storageType: StorageType | null | undefined
): NumericRange | null => {
  switch (storageType) {
    case StorageType.IO1:
      return { min: IO1_MIN_DISK_IOPS, max: IO1_MAX_DISK_IOPS };
    case StorageType.IO2:
      return { min: IO2_MIN_DISK_IOPS, max: IO2_MAX_DISK_IOPS };
    case StorageType.GP3:
      return { min: GP3_MIN_DISK_IOPS, max: GP3_MAX_IOPS };
    case StorageType.Hyperdisk_Balanced:
      return { min: HB_MIN_DISK_IOPS, max: HB_MAX_DISK_IOPS };
    case StorageType.Hyperdisk_Extreme:
      return { min: HE_MIN_DISK_IOPS, max: HE_MAX_DISK_IOPS };
    case StorageType.PremiumV2_LRS:
      return { min: PremiumV2_LRS_MIN_DISK_IOPS, max: PremiumV2_LRS_MAX_DISK_IOPS };
    case StorageType.UltraSSD_LRS:
      return { min: UltraSSD_MIN_DISK_IOPS, max: UltraSSD_MAX_DISK_IOPS };
    default:
      return null;
  }
};

export const getThroughputRange = (
  storageType: StorageType | null | undefined
): NumericRange | null => {
  switch (storageType) {
    case StorageType.GP3:
      return { min: GP3_MIN_THROUGHPUT, max: GP3_MAX_THROUGHPUT };
    case StorageType.Hyperdisk_Balanced:
      return { min: HB_MIN_THROUGHPUT, max: HB_DISK_THROUGHPUT_CAP };
    case StorageType.PremiumV2_LRS:
      return { min: PremiumV2_LRS_MIN_THROUGHPUT, max: PremiumV2_LRS_DISK_THROUGHPUT_CAP };
    case StorageType.UltraSSD_LRS:
      return { min: UltraSSD_MIN_THROUGHPUT, max: UltraSSD_DISK_THROUGHPUT_CAP };
    default:
      return null;
  }
};

export interface StorageTypeOption {
  value: StorageType;
  label: string;
}

export const DEFAULT_STORAGE_TYPES = {
  [CloudType.aws]: StorageType.GP3,
  [CloudType.gcp]: StorageType.Persistent,
  [CloudType.azu]: StorageType.Premium_LRS,
  [CloudType.oci]: StorageType.OCI_BALANCED
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

export const OCI_STORAGE_TYPE_OPTIONS: StorageTypeOption[] = [
  { value: StorageType.OCI_BALANCED, label: 'Balanced' },
  { value: StorageType.OCI_HIGHERPERFORMANCE, label: 'Higher Performance' },
  { value: StorageType.OCI_LOWERCOST, label: 'Lower Cost' }
];

export const DEFAULT_OCI_VOLUME_SIZE_GB = 250;

export const getMinDiskIops = (storageType: StorageType, volumeSize: number) => {
  const range = getDiskIopsRange(storageType);
  if (!range) return 0;
  // azure also floors iops by volume size
  switch (storageType) {
    case StorageType.UltraSSD_LRS:
    case StorageType.PremiumV2_LRS:
      return Math.max(range.min, volumeSize);
    default:
      return range.min;
  }
};

export const getMaxDiskIops = (storageType: StorageType, volumeSize?: number | null) => {
  const range = getDiskIopsRange(storageType);
  if (!range) return undefined;
  if (volumeSize == null || !Number.isFinite(volumeSize) || volumeSize <= 0) {
    return range.max;
  }
  // don't clamp below the backend min (small volumes)
  const clampMax = (perGbCap: number) =>
    Math.max(range.min, Math.min(range.max, perGbCap * volumeSize));

  switch (storageType) {
    case StorageType.UltraSSD_LRS:
      return clampMax(UltraSSD_DISK_IOPS_MAX_PER_GB);
    case StorageType.PremiumV2_LRS:
      return clampMax(PremiumV2_LRS_DISK_IOPS_MAX_PER_GB);
    case StorageType.Hyperdisk_Balanced:
      return clampMax(HB_DISK_IOPS_MAX_PER_GB);
    case StorageType.Hyperdisk_Extreme:
      return clampMax(HE_DISK_IOPS_MAX_PER_GB);
    default:
      return range.max;
  }
};

export const getMinThroughput = (storageType: StorageType) => {
  return getThroughputRange(storageType)?.min ?? 0;
};

export const getMaxThroughput = (storageType: StorageType) => {
  return getThroughputRange(storageType)?.max;
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
    case CloudType.oci:
      return OCI_STORAGE_TYPE_OPTIONS;
    default:
      return [];
  }
};

export const getIopsByStorageType = (storageType: StorageType) => {
  if (storageType === StorageType.IO1) {
    return IO1_DEFAULT_DISK_IOPS;
  } else if (storageType === StorageType.IO2) {
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

const clampThroughputToIopsRatio = (
  currentThroughput: number,
  diskIops: number,
  iopsToThroughputRatio: number,
  absoluteMax: number,
  storageType: StorageType
) => {
  const range = getThroughputRange(storageType);
  const maxThroughput = Math.min(
    Math.floor(diskIops / iopsToThroughputRatio),
    absoluteMax
  );
  if (range && maxThroughput < range.min) {
    return currentThroughput;
  }
  return Math.max(0, Math.min(maxThroughput, currentThroughput));
};

export const getThroughputByIops = (
  currentThroughput: number,
  diskIops: number,
  storageType: StorageType
) => {
  // keep throughput as int
  if (storageType === StorageType.GP3) {
    if (
      currentThroughput > 0 &&
      (diskIops > GP3_DEFAULT_DISK_IOPS || currentThroughput > GP3_DEFAULT_DISK_THROUGHPUT) &&
      diskIops / currentThroughput < GP3_IOPS_TO_MAX_DISK_THROUGHPUT
    ) {
      return Math.min(
        GP3_MAX_THROUGHPUT,
        Math.max(
          Math.floor(diskIops / GP3_IOPS_TO_MAX_DISK_THROUGHPUT),
          GP3_DEFAULT_DISK_THROUGHPUT
        )
      );
    }
  } else if (storageType === StorageType.UltraSSD_LRS) {
    return clampThroughputToIopsRatio(
      currentThroughput,
      diskIops,
      UltraSSD_IOPS_TO_MAX_DISK_THROUGHPUT,
      UltraSSD_DISK_THROUGHPUT_CAP,
      storageType
    );
  } else if (storageType === StorageType.PremiumV2_LRS) {
    return clampThroughputToIopsRatio(
      currentThroughput,
      diskIops,
      PremiumV2_LRS_IOPS_TO_MAX_DISK_THROUGHPUT,
      PremiumV2_LRS_DISK_THROUGHPUT_CAP,
      storageType
    );
  } else if (storageType === StorageType.Hyperdisk_Balanced) {
    return clampThroughputToIopsRatio(
      currentThroughput,
      diskIops,
      HB_IOPS_TO_MAX_DISK_THROUGHPUT,
      HB_DISK_THROUGHPUT_CAP,
      storageType
    );
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
  } else if (instance.providerCode === CloudType.oci) {
    volumeSize = DEFAULT_OCI_VOLUME_SIZE_GB;
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
  } else if (instance.providerCode === CloudType.oci) {
    storageType = DEFAULT_STORAGE_TYPES[CloudType.oci];
  }
  return storageType;
};

export const getDeviceInfoFromInstance = (
  instance: InstanceType,
  providerRuntimeConfigs: any
): DeviceInfo | null => {
  if (!instance.instanceTypeDetails.volumeDetailsList.length) return null;

  const { volumeDetailsList } = instance.instanceTypeDetails;
  const volumeSize = volumeDetailsList[0].volumeSizeGB;
  const defaultInstanceVolumeSize = isEphemeralAwsStorageInstance(instance)
    ? volumeSize
    : getVolumeSize(instance, providerRuntimeConfigs);
  const storageType = getStorageType(instance, providerRuntimeConfigs);
  // Disk IOPS does not exist for all storage types
  const diskIops = getIopsByStorageType(storageType);
  // Throughput does not exist for all storage types
  const throughput = getThroughputByStorageType(storageType);

  const resolvedVolumeSize = Number(defaultInstanceVolumeSize ?? volumeSize);

  return {
    numVolumes: volumeDetailsList.length,
    // runtime configs come back as strings
    volumeSize: Number.isFinite(resolvedVolumeSize) ? resolvedVolumeSize : null,
    storageClass: 'standard',
    storageType,
    mountPoints:
      instance.providerCode === CloudType.onprem
        ? volumeDetailsList.flatMap((item) => item.mountPath).join(',')
        : null,
    diskIops: diskIops,
    throughput: throughput
  };
};

export const useVolumeControls = () => {
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
    if (provider?.code !== CloudType.kubernetes) {
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
