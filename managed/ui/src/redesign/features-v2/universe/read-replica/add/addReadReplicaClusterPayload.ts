import _ from 'lodash';
import {
  ClusterAddReqBody,
  ClusterAddSpecClusterType,
  ClusterEditSpec,
  ClusterGFlags,
  ClusterNodeSpec,
  ClusterPartitionSpec,
  ClusterPlacementSpec,
  ClusterSpec,
  ClusterSpecClusterType,
  PlacementAZ,
  PlacementCloud,
  PlacementRegion
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  buildStorageSpecFromDeviceInfo,
  mapGFlags
} from '@app/redesign/features-v2/universe/create-universe/CreateUniverseUtils';
import { AddRRContextProps } from './AddReadReplicaContext';
import { RRRegionsAndAZSettings } from './steps/RRRegionsAndAZ/dtos';
import { RRInstanceSettingsProps } from './steps/RRInstanceSettings/RRInstanceSettings';
import { getClusterByType } from '../../edit-universe/EditUniverseUtils';
import { getReadOnlyCluster } from '@app/redesign/utils/universeUtils';
import { getExistingGeoPartitions } from '../../geo-partition/add/AddGeoPartitionUtils';

export function sumReadReplicaNodeCounts(regionsAndAZ: RRRegionsAndAZSettings): number {
  return regionsAndAZ.regions.reduce(
    (acc, r) =>
      acc +
      r.zones.reduce((a, z) => {
        const n = Number(z.nodeCount);
        return a + (Number.isFinite(n) && n >= 1 ? Math.floor(n) : 1);
      }, 0),
    0
  );
}

/** Read replicas use T-Server flags only; reuse create-universe `mapGFlags` then drop master. */
function mapReadReplicaGFlags(
  customizeRRFlags: boolean,
  gFlags: { Name: string; MASTER?: string | boolean | number; TSERVER?: string | boolean | number }[]
): ClusterGFlags | undefined {
  if (!customizeRRFlags || !gFlags?.length) {
    return undefined;
  }
  const { tserver } = mapGFlags(gFlags);
  if (!Object.keys(tserver).length) {
    return undefined;
  }
  return { tserver };
}

/** Sum of per-AZ replication_factor; must match partition/cluster RF for YBA placement validation. */
export function sumPlacementAzReplicationFactors(regionList: PlacementRegion[]): number {
  return regionList.reduce(
    (sum, region) =>
      sum +
      (region.az_list ?? []).reduce((acc, az) => acc + (az.replication_factor ?? 0), 0),
    0
  );
}

function buildPlacementRegions(
  regionsAndAZ: RRRegionsAndAZSettings,
  providerRegions: Region[]
): PlacementRegion[] {
  const byUuid = new Map(providerRegions.map((r) => [r.uuid, r]));
  return regionsAndAZ.regions.map((r) => {
      if (!r.regionUuid) {
        throw new Error('READ_REPLICA_REGION_REQUIRED');
      }
      const meta = byUuid.get(r.regionUuid);
      if (!meta) {
        throw new Error(`Unknown region: ${r.regionUuid}`);
      }
      const az_list: PlacementAZ[] = r.zones.map((z) => {
        if (!z.zoneUuid) {
          throw new Error('READ_REPLICA_AZ_REQUIRED');
        }
        const azMeta = meta.zones.find((zz) => zz.uuid === z.zoneUuid);
        if (!azMeta) {
          throw new Error(`Unknown availability zone: ${z.zoneUuid}`);
        }
        const rawNodes = Number(z.nodeCount);
        const num_nodes_in_az =
          Number.isFinite(rawNodes) && rawNodes >= 1 ? Math.floor(rawNodes) : 1;
        const rawRf = Number(z.dataCopies);
        const rf =
          Number.isFinite(rawRf) && rawRf >= 1
            ? Math.floor(rawRf)
            : 1;
        const replication_factor = Math.max(1, rf);
        return {
          uuid: z.zoneUuid,
          name: azMeta.name,
          subnet: azMeta.subnet,
          replication_factor,
          num_nodes_in_az
        };
      });
      return {
        uuid: r.regionUuid,
        code: String(meta.code),
        name: meta.name,
        az_list
      };
    });
}

function buildReadReplicaNodeSpec(
  primary: ClusterSpec | undefined,
  instanceSettings: RRInstanceSettingsProps
): ClusterNodeSpec {
  if (instanceSettings.inheritPrimaryInstance && primary?.node_spec) {
    return _.cloneDeep(primary.node_spec) as ClusterNodeSpec;
  }
  const { instanceType, deviceInfo, enableEbsVolumeEncryption, ebsKmsConfigUUID } =
    instanceSettings;
  if (!deviceInfo || !instanceType) {
    throw new Error('READ_REPLICA_INSTANCE_REQUIRED');
  }
  return {
    instance_type: instanceType,
    storage_spec: buildStorageSpecFromDeviceInfo(
      deviceInfo,
      enableEbsVolumeEncryption,
      ebsKmsConfigUUID
    )
  };
}

type ReadReplicaSizingAndPlacement = {
  num_nodes: number;
  node_spec: ClusterNodeSpec;
  provider_spec: NonNullable<ClusterEditSpec['provider_spec']>;
  placement_spec?: ClusterPlacementSpec;
  partitions_spec?: ClusterPartitionSpec[];
};

function getExistingAsyncPlacementCloud(
  cluster: ClusterSpec | null | undefined
): PlacementCloud | undefined {
  const fromPlacement = cluster?.placement_spec?.cloud_list?.[0];
  if (fromPlacement) {
    return fromPlacement;
  }
  const defaultPart =
    cluster?.partitions_spec?.find((p) => p.default_partition) ?? cluster?.partitions_spec?.[0];
  return defaultPart?.placement?.cloud_list?.[0];
}

function getReadReplicaReplicationFactor(cluster: ClusterSpec | null | undefined): number {
  const defaultPart =
    cluster?.partitions_spec?.find((p) => p.default_partition) ?? cluster?.partitions_spec?.[0];
  const partitionRf = Number(defaultPart?.replication_factor);
  if (Number.isFinite(partitionRf) && partitionRf >= 1) {
    return Math.floor(partitionRf);
  }
  const clusterRf = Number(cluster?.replication_factor);
  if (Number.isFinite(clusterRf) && clusterRf >= 1) {
    return Math.floor(clusterRf);
  }
  return 1;
}

export function buildReadReplicaClusterSizingAndPlacement(
  context: AddRRContextProps,
  providerRegions: Region[]
): ReadReplicaSizingAndPlacement {
  const { universeData, regionsAndAZ, instanceSettings } = context;
  if (!universeData?.spec) {
    throw new Error('READ_REPLICA_UNIVERSE_MISSING');
  }
  if (!instanceSettings) {
    throw new Error('READ_REPLICA_INSTANCE_SETTINGS_MISSING');
  }
  if (!regionsAndAZ?.regions?.length) {
    throw new Error('READ_REPLICA_PLACEMENT_MISSING');
  }

  const primary = getClusterByType(universeData, ClusterSpecClusterType.PRIMARY) as
    | ClusterSpec
    | undefined;
  const providerUuid = primary?.provider_spec?.provider;
  if (!providerUuid) {
    throw new Error('READ_REPLICA_PROVIDER_MISSING');
  }

  const primaryCloud =
    primary?.placement_spec?.cloud_list?.[0] ??
    (primary?.partitions_spec?.find((p) => p.default_partition) ?? primary?.partitions_spec?.[0])
      ?.placement?.cloud_list?.[0];
  const cloudCode = primaryCloud?.code;
  if (!cloudCode) {
    throw new Error('READ_REPLICA_CLOUD_CODE_MISSING');
  }

  const regionList = buildPlacementRegions(regionsAndAZ, providerRegions);
  if (!regionList.length) {
    throw new Error('READ_REPLICA_PLACEMENT_MISSING');
  }

  const placementRegionUuids = regionList.map((r) => r.uuid).filter(Boolean) as string[];
  const existingAsync = getReadOnlyCluster(universeData.spec?.clusters ?? []);
  const existingCloud = getExistingAsyncPlacementCloud(existingAsync);
  const defaultRegion =
    existingCloud?.default_region &&
    placementRegionUuids.includes(existingCloud.default_region)
      ? existingCloud.default_region
      : regionList[0].uuid;

  const num_nodes = sumReadReplicaNodeCounts(regionsAndAZ);
  if (num_nodes < 1) {
    throw new Error('READ_REPLICA_NODE_COUNT_INVALID');
  }

  const hasInvalidAzReplicationFactor = regionList.some((region) =>
    (region.az_list ?? []).some(
      (az) => (az.replication_factor ?? 0) > (az.num_nodes_in_az ?? 0)
    )
  );
  if (hasInvalidAzReplicationFactor) {
    throw new Error('READ_REPLICA_REPLICATION_FACTOR_EXCEEDS_NODES');
  }

  const provider_spec = {
    region_list: placementRegionUuids
  };

  const node_spec = buildReadReplicaNodeSpec(primary, instanceSettings);

  const cloudEntry = {
    uuid: providerUuid,
    code: cloudCode,
    default_region: defaultRegion,
    ...(typeof existingCloud?.masters_in_default_region === 'boolean'
      ? { masters_in_default_region: existingCloud.masters_in_default_region }
      : {}),
    region_list: regionList
  } as PlacementCloud;

  const isGeoPartitioned = getExistingGeoPartitions(universeData).length > 0;

  if (isGeoPartitioned) {
    const placementReplicaTotal = Math.max(1, sumPlacementAzReplicationFactors(regionList));
    const partitionPlacement: ClusterPlacementSpec = { cloud_list: [cloudEntry] };
    const partitions_spec: ClusterPartitionSpec[] = [
      {
        name: '',
        default_partition: true,
        replication_factor: placementReplicaTotal,
        placement: partitionPlacement,
        tablespace_name: ''
      }
    ];
    return { num_nodes, node_spec, provider_spec, partitions_spec };
  }

  const placement_spec: ClusterPlacementSpec = { cloud_list: [cloudEntry] };
  return { num_nodes, node_spec, provider_spec, placement_spec };
}

/**
 * Maps Add Read Replica wizard state to POST /universes/:uuid/clusters payload.
 */
export function mapAddReadReplicaClusterPayload(
  context: AddRRContextProps,
  providerRegions: Region[]
): ClusterAddReqBody {
  const { databaseSettings } = context;
  const sizingAndPlacement = buildReadReplicaClusterSizingAndPlacement(context, providerRegions);

  const gflags = mapReadReplicaGFlags(
    !!databaseSettings?.customizeRRFlags,
    databaseSettings?.gFlags ?? []
  );

  const payload: ClusterAddReqBody = {
    cluster_type: ClusterAddSpecClusterType.READ_REPLICA,
    ...sizingAndPlacement,
    ...(gflags ? { gflags } : {})
  };

  return payload;
}

export function mapEditReadReplicaClusterSpec(
  rrClusterUuid: string,
  context: AddRRContextProps,
  providerRegions: Region[],
  options?: { enforceNumNodesFloor?: boolean }
): ClusterEditSpec {
  const sizingAndPlacement = buildReadReplicaClusterSizingAndPlacement(context, providerRegions);
  const enforceNumNodesFloor = options?.enforceNumNodesFloor ?? false;
  if (!enforceNumNodesFloor) {
    return {
      uuid: rrClusterUuid,
      ...sizingAndPlacement
    };
  }
  const existingReadReplica =
    (context.universeData?.spec?.clusters ?? []).find((cluster) => cluster.uuid === rrClusterUuid) ??
    getReadOnlyCluster(context.universeData?.spec?.clusters ?? []);
  const existingReadReplicaRf = getReadReplicaReplicationFactor(existingReadReplica);
  const num_nodes = Math.max(sizingAndPlacement.num_nodes, existingReadReplicaRf);

  return {
    uuid: rrClusterUuid,
    ...sizingAndPlacement,
    num_nodes
  };
}
