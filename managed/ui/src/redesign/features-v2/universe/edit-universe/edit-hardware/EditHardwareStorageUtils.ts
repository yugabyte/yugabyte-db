import { DeviceInfo } from '../../../../features/universe/universe-form/utils/dto';
import { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import {
  ClusterResizeStorageSpec,
  ClusterSpec,
  ClusterStorageSpec
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

/**
 * Normalized intermediate representation of a storage spec used to compare and
 * render hardware diffs. Keeps `null` for empty values so that `isEqual` works
 * across both API payload (snake_case) and form values (camelCase) sources.
 */
export type NormalizedStorage = {
  volumeSize: number | null;
  numVolumes: number | null;
  diskIops: number | null;
  throughput: number | null;
  storageClass: string | null;
  storageType: string | null;
  mountPoints: string | null;
};

export const normalizeStorageType = (value: unknown): string | null =>
  value === undefined || value === null ? null : String(value);

export const normalizeClusterStorage = (
  spec: ClusterStorageSpec | undefined
): NormalizedStorage => ({
  volumeSize: spec?.volume_size ?? null,
  numVolumes: spec?.num_volumes ?? null,
  diskIops: spec?.disk_iops ?? null,
  throughput: spec?.throughput ?? null,
  storageClass: spec?.storage_class ?? null,
  storageType: normalizeStorageType(spec?.storage_type),
  mountPoints: spec?.mount_points ?? null
});

export const normalizeDeviceInfo = (
  deviceInfo: DeviceInfo | null | undefined
): NormalizedStorage => ({
  volumeSize: deviceInfo?.volumeSize ?? null,
  numVolumes: deviceInfo?.numVolumes ?? null,
  diskIops: deviceInfo?.diskIops ?? null,
  throughput: deviceInfo?.throughput ?? null,
  storageClass: deviceInfo?.storageClass ?? null,
  storageType: normalizeStorageType(deviceInfo?.storageType),
  mountPoints: deviceInfo?.mountPoints ?? null
});

/** Fields supported by /resize_nodes only (volume size, iops, throughput). */
export const toResizeStorageSpec = (
  deviceInfo: DeviceInfo | null | undefined,
  currentSpec: ClusterStorageSpec | undefined
): ClusterResizeStorageSpec => ({
  volume_size: deviceInfo?.volumeSize ?? currentSpec?.volume_size,
  disk_iops: deviceInfo?.diskIops ?? currentSpec?.disk_iops ?? undefined,
  throughput: deviceInfo?.throughput ?? currentSpec?.throughput ?? undefined
});

/** Full storage payload for edit-universe (includes storage_type, num_volumes, etc.). */
export const toClusterStorageSpec = (
  deviceInfo: DeviceInfo | null | undefined,
  currentSpec: ClusterStorageSpec | undefined
): ClusterStorageSpec => ({
  volume_size: deviceInfo?.volumeSize ?? currentSpec?.volume_size ?? 0,
  num_volumes: deviceInfo?.numVolumes ?? currentSpec?.num_volumes ?? 1,
  ...(deviceInfo?.mountPoints ?? currentSpec?.mount_points
    ? { mount_points: deviceInfo?.mountPoints ?? currentSpec?.mount_points }
    : {}),
  ...(deviceInfo?.storageClass ?? currentSpec?.storage_class
    ? { storage_class: deviceInfo?.storageClass ?? currentSpec?.storage_class }
    : {}),
  ...(deviceInfo?.storageType ?? currentSpec?.storage_type
    ? { storage_type: deviceInfo?.storageType ?? currentSpec?.storage_type }
    : {}),
  ...(deviceInfo?.diskIops !== undefined && deviceInfo?.diskIops !== null
    ? { disk_iops: deviceInfo.diskIops }
    : currentSpec?.disk_iops !== undefined && currentSpec?.disk_iops !== null
      ? { disk_iops: currentSpec.disk_iops }
      : {}),
  ...(deviceInfo?.throughput !== undefined && deviceInfo?.throughput !== null
    ? { throughput: deviceInfo.throughput }
    : currentSpec?.throughput !== undefined && currentSpec?.throughput !== null
      ? { throughput: currentSpec.throughput }
      : {}),
  ...(currentSpec?.cloud_volume_encryption
    ? { cloud_volume_encryption: currentSpec.cloud_volume_encryption }
    : {})
});

/**
 * True when storage fields not supported by ClusterResizeStorageSpec changed
 * (num_volumes, storage_type, or storage_class). Those edits must go through edit-universe.
 */
export const storageRequiresEditUniverse = (
  settings: InstanceSettingProps,
  targetCluster: ClusterSpec | undefined,
  dedicatedNodes: boolean
): boolean => {
  const currentTserver = normalizeClusterStorage(targetCluster?.node_spec?.storage_spec);
  const nextTserver = normalizeDeviceInfo(settings.deviceInfo);

  if (
    nextTserver.numVolumes !== currentTserver.numVolumes ||
    nextTserver.storageType !== currentTserver.storageType ||
    nextTserver.storageClass !== currentTserver.storageClass
  ) {
    return true;
  }

  if (!dedicatedNodes) {
    return false;
  }

  const currentMaster = normalizeClusterStorage(
    targetCluster?.node_spec?.master?.storage_spec ?? targetCluster?.node_spec?.storage_spec
  );
  const nextMaster = normalizeDeviceInfo(settings.masterDeviceInfo ?? settings.deviceInfo);

  return (
    nextMaster.numVolumes !== currentMaster.numVolumes ||
    nextMaster.storageType !== currentMaster.storageType ||
    nextMaster.storageClass !== currentMaster.storageClass
  );
};
