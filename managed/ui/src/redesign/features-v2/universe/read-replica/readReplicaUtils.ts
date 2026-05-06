import _ from 'lodash';
import { ClusterSpec, UniverseRespResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import type { DeviceInfo, K8NodeSpec } from '@app/redesign/features/universe/universe-form/utils/dto';
import { StorageType } from '@app/redesign/helpers/dtos';
import type { RRInstanceSettingsProps } from './add/steps/RRInstanceSettings/RRInstanceSettings';

const STORAGE_KEYS_FOR_COMPARE = [
  'volume_size',
  'num_volumes',
  'disk_iops',
  'throughput',
  'storage_type',
  'storage_class'
] as const;

export function getAddReadReplicaRoute(universeUuid?: string | null) {
  return `/universes/${universeUuid ?? ''}/add-read-replica`;
}

export function getReadReplicaExitRoute(universeUuid?: string | null) {
  return `/universes/${universeUuid ?? ''}/settings`;
}

export function getK8TserverResourceFromNodeSpec(
  nodeSpec: ClusterSpec['node_spec']
): K8NodeSpec | null {
  if (!nodeSpec) return null;
  const rec = nodeSpec as Record<string, unknown>;
  const raw = (rec.k8s_tserver_resource_spec ?? rec.k8sTserverResourceSpec) as
    | Record<string, unknown>
    | undefined;
  if (!raw || typeof raw !== 'object') return null;
  const cpu = raw.cpu_core_count ?? raw.cpuCoreCount;
  const mem = raw.memory_gib ?? raw.memoryGib;
  if ((cpu === null || cpu === undefined) && (mem === null || mem === undefined)) return null;
  const cpuCoreCount = Number(cpu);
  const memoryGib = Number(mem);
  if (!Number.isFinite(cpuCoreCount) || !Number.isFinite(memoryGib)) return null;
  return { cpuCoreCount, memoryGib };
}

export function buildRRInstanceSettingsFromCluster(
  cluster: ClusterSpec,
  arch: RRInstanceSettingsProps['arch'],
  inheritPrimaryInstance: boolean
): RRInstanceSettingsProps {
  const storageSpec = cluster.node_spec?.storage_spec;
  const cloudVolumeEncryption = storageSpec?.cloud_volume_encryption;
  const k8 = getK8TserverResourceFromNodeSpec(cluster.node_spec);
  return {
    inheritPrimaryInstance,
    arch,
    instanceType: cluster.node_spec?.instance_type ?? null,
    useSpotInstance: cluster.use_spot_instance ?? false,
    deviceInfo: storageSpec
      ? ({
          volumeSize: storageSpec.volume_size,
          numVolumes: storageSpec.num_volumes,
          diskIops: storageSpec.disk_iops ?? null,
          throughput: storageSpec.throughput ?? null,
          storageClass: 'standard',
          storageType: (storageSpec.storage_type as StorageType) ?? null
        } satisfies DeviceInfo)
      : null,
    enableEbsVolumeEncryption: cloudVolumeEncryption?.enable_volume_encryption ?? false,
    ebsKmsConfigUUID: cloudVolumeEncryption?.kms_config_uuid ?? null,
    ...(k8 ? { tserverK8SNodeResourceSpec: k8 } : {})
  };
}

export function readReplicaHardwareMatchesPrimary(
  primary: ClusterSpec | undefined | null,
  asyncCluster: ClusterSpec
): boolean {
  if (!primary?.node_spec || !asyncCluster.node_spec) return false;
  const ps = primary.node_spec.storage_spec;
  const as = asyncCluster.node_spec.storage_spec;
  const storageMatch = _.isEqual(
    _.pick(ps ?? {}, STORAGE_KEYS_FOR_COMPARE),
    _.pick(as ?? {}, STORAGE_KEYS_FOR_COMPARE)
  );
  const spotMatch = Boolean(primary.use_spot_instance) === Boolean(asyncCluster.use_spot_instance);
  const instMatch = primary.node_spec.instance_type === asyncCluster.node_spec.instance_type;
  const k8Match = _.isEqual(
    getK8TserverResourceFromNodeSpec(primary.node_spec),
    getK8TserverResourceFromNodeSpec(asyncCluster.node_spec)
  );
  return storageMatch && spotMatch && instMatch && k8Match;
}
