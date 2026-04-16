import { RunTimeConfigEntry, DeviceInfo } from '../../../utils/dto';

const getK8VolumeInfo = (providerRuntimeConfigs: any) => {
  const volumeSize = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.default_volume_size_gb'
    )?.value;
  const volumeCount = providerRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.default_volume_count'
    )?.value; 
  return { volumeSize, volumeCount };
}

export const getK8DeviceInfo = (providerRuntimeConfigs: any): DeviceInfo => {
  const { volumeSize, volumeCount } = getK8VolumeInfo(providerRuntimeConfigs); 
  return {
    volumeSize: volumeSize,
    numVolumes: volumeCount,
    storageClass: 'standard',
    storageType: null,
    mountPoints: null,
    diskIops: null,
    throughput: null
  };
}
