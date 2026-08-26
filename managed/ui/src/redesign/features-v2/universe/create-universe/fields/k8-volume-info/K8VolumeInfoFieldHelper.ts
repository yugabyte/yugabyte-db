import {
  DeviceInfo,
  RunTimeConfigEntry
} from '@app/redesign/features/universe/universe-form/utils/dto';

const getK8VolumeInfo = (providerRuntimeConfigs: any) => {
  const volumeSize = providerRuntimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.default_volume_size_gb'
  )?.value;

  const volumeCount = providerRuntimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.default_volume_count'
  )?.value;

  return { volumeSize, volumeCount };
};

export const getK8DeviceInfo = (providerRuntimeConfigs: any): DeviceInfo => {
  const { volumeSize, volumeCount } = getK8VolumeInfo(providerRuntimeConfigs);
  const resolvedVolumeSize = Number(volumeSize);
  const resolvedVolumeCount = Number(volumeCount);
  return {
    // runtime configs come back as strings
    volumeSize: Number.isFinite(resolvedVolumeSize) ? resolvedVolumeSize : null,
    numVolumes: Number.isFinite(resolvedVolumeCount) ? resolvedVolumeCount : null,
    storageClass: 'standard',
    storageType: null,
    mountPoints: null,
    diskIops: null,
    throughput: null
  };
};
