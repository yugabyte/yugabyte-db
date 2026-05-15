import { useContext } from 'react';
import { EditPlacementContext, EditPlacementContextMethods } from './EditPlacementContext';
import {
  ClusterSpec,
  ClusterSpecClusterType,
  Universe,
  ClusterPartitionSpec,
  ClusterPlacementSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { Region } from '@app/redesign/helpers/dtos';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import {
  getEffectiveReplicationFactorForResilience,
  getNodeCount,
  getPlacementRegions
} from '../../create-universe/CreateUniverseUtils';
import {
  getExistingGeoPartitions
} from '../../geo-partition/add/AddGeoPartitionUtils';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  getNodeAvailabilityDefaultsFromClusterPlacement,
  mapUniversePayloadToResilienceAndRegionsProps
} from '../EditUniverseUtils';

export const useGetEditPlacementContext = (): EditPlacementContextMethods => {
  const context = useContext(EditPlacementContext);
  if (!context) {
    throw new Error('useGetEditPlacementContext must be used within an EditPlacementProvider');
  }
  return (context as unknown) as EditPlacementContextMethods;
};

export const getResilienceAndRegionsProps = (
  universeData: Universe,
  providerRegions: Region[],
  selectedPartitionUUID?: string
): ResilienceAndRegionsProps => {
  const hasGeoPartitions = getExistingGeoPartitions(universeData!).length > 0;
  const primaryCluster = getClusterByType(universeData, ClusterSpecClusterType.PRIMARY);
  if (hasGeoPartitions) {
    const selectedPartition = primaryCluster?.partitions_spec?.find(
      (partition) => partition.uuid === selectedPartitionUUID
    );
    if (!selectedPartition?.placement) {
      throw new Error('Selected partition placement data is missing');
    }
    const effectiveReplicationFactor =
      selectedPartition.replication_factor ?? primaryCluster?.replication_factor ?? 1;
    const stats = countRegionsAzsAndNodes(selectedPartition.placement);
    return mapUniversePayloadToResilienceAndRegionsProps(
      providerRegions!,
      stats,
      {
        ...selectedPartition,
        replication_factor: effectiveReplicationFactor
      }
    );
  } else {
    const stats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);
    return mapUniversePayloadToResilienceAndRegionsProps(providerRegions!, stats, primaryCluster!);
  }
};

/** Defaults for edit-placement nodes step (honors dedicated nodes and geo partition/default partition placement). */
export const getNodesAvailabilityDefaultsForEditPlacement = (
  universeData: Universe,
  selectedPartitionUUID?: string
): NodeAvailabilityProps => {
  const primaryCluster = getClusterByType(universeData, ClusterSpecClusterType.PRIMARY);
  if (!primaryCluster) {
    throw new Error('Primary cluster is missing');
  }

  const partitionUuidForGeo =
    selectedPartitionUUID ??
    primaryCluster.partitions_spec?.find((p) => p.default_partition)?.uuid ??
    primaryCluster.partitions_spec?.[0]?.uuid;

  const selectedPartition = partitionUuidForGeo
    ? primaryCluster.partitions_spec?.find((p) => p.uuid === partitionUuidForGeo)
    : undefined;

  const placementForDefaults = selectedPartition?.placement ?? primaryCluster.placement_spec;
  if (!placementForDefaults) {
    throw new Error('Primary cluster placement data is missing');
  }

  const defaults = getNodeAvailabilityDefaultsFromClusterPlacement(
    primaryCluster,
    placementForDefaults,
    selectedPartition?.replication_factor
  );

  return defaults;
};

const buildPlacementSpecFromRegionList = (
  existingPlacementSpec: ClusterPlacementSpec,
  regionList: ReturnType<typeof getPlacementRegions>
): ClusterPlacementSpec => {
  const cloud = existingPlacementSpec?.cloud_list?.[0];
  if (!cloud) {
    throw new Error('Cloud placement is missing in cluster placement spec');
  }

  return {
    cloud_list: [
      {
        ...cloud,
        default_region: regionList[0]?.uuid ?? cloud.default_region,
        region_list: regionList
      }
    ]
  };
};

export const buildPrimaryPlacementEditPayload = (
  universeData: Universe,
  resilience: ResilienceAndRegionsProps,
  nodesAndAvailability?: NodeAvailabilityProps
) => {
  const primaryCluster = getClusterByType(universeData, ClusterSpecClusterType.PRIMARY);
  if (!primaryCluster?.placement_spec || !primaryCluster.uuid) {
    throw new Error('Primary cluster placement data is missing');
  }

  const regionList = getPlacementRegions(resilience, nodesAndAvailability?.availabilityZones);

  return {
    clusterUUID: primaryCluster.uuid,
    placementSpec: buildPlacementSpecFromRegionList(primaryCluster.placement_spec, regionList)
  };
};

export const buildGeoPartitionPlacementEditPayload = (
  universeData: Universe,
  selectedPartitionUUID: string,
  resilience: ResilienceAndRegionsProps,
  nodesAndAvailability?: NodeAvailabilityProps
) => {
  const primaryCluster = getClusterByType(universeData, ClusterSpecClusterType.PRIMARY);
  if (!primaryCluster?.uuid || !primaryCluster.partitions_spec?.length) {
    throw new Error('Primary cluster partitions are missing');
  }

  const selectedPartition = primaryCluster.partitions_spec.find(
    (partition) => partition.uuid === selectedPartitionUUID
  );
  if (!selectedPartition?.placement) {
    throw new Error('Selected partition placement data is missing');
  }

  const regionList = getPlacementRegions(resilience, nodesAndAvailability?.availabilityZones);
  const updatedPlacement = buildPlacementSpecFromRegionList(selectedPartition.placement, regionList);
  const effectiveReplicationFactor = getEffectiveReplicationFactorForResilience(resilience);

  const partitionsSpec: ClusterPartitionSpec[] = primaryCluster.partitions_spec.map((partition) =>
    partition.uuid === selectedPartitionUUID
      ? {
          ...partition,
          replication_factor: effectiveReplicationFactor,
          placement: updatedPlacement
        }
      : partition
  );

  return {
    clusterUUID: primaryCluster.uuid,
    partitionsSpec
  };
};

export type MasterAllocationEditMutationCluster = {
  uuid: string;
  num_nodes: number;
  node_spec: NonNullable<ClusterSpec['node_spec']> & { dedicated_nodes?: boolean };
  placement_spec?: ClusterPlacementSpec;
  partitions_spec?: ClusterPartitionSpec[];
};

const toClusterStorageSpec = (
  currentStorageSpec: NonNullable<ClusterSpec['node_spec']>['storage_spec'] | undefined,
  deviceInfo: InstanceSettingProps['deviceInfo'] | undefined
) => ({
  ...currentStorageSpec,
  volume_size: deviceInfo?.volumeSize ?? currentStorageSpec?.volume_size,
  num_volumes: deviceInfo?.numVolumes ?? currentStorageSpec?.num_volumes,
  disk_iops: deviceInfo?.diskIops ?? currentStorageSpec?.disk_iops,
  throughput: deviceInfo?.throughput ?? currentStorageSpec?.throughput,
  storage_class: deviceInfo?.storageClass ?? currentStorageSpec?.storage_class,
  storage_type: deviceInfo?.storageType ?? currentStorageSpec?.storage_type,
  mount_points: deviceInfo?.mountPoints ?? currentStorageSpec?.mount_points
});

/** Edit-universe cluster fragment for master allocation (placement + dedicated_nodes + num_nodes). */
export const buildMasterAllocationEditPayload = (
  universeData: Universe,
  providerRegions: Region[],
  nodesAndAvailability: NodeAvailabilityProps,
  selectedPartitionUUID?: string,
  instanceSettings?: InstanceSettingProps | null
): MasterAllocationEditMutationCluster => {
  const primaryCluster = getClusterByType(universeData, ClusterSpecClusterType.PRIMARY);
  if (!primaryCluster?.uuid) {
    throw new Error('Primary cluster is missing');
  }

  const num_nodes = getNodeCount(nodesAndAvailability.availabilityZones);
  const node_spec = {
    ...(primaryCluster.node_spec ?? {}),
    dedicated_nodes: nodesAndAvailability.useDedicatedNodes
  } as MasterAllocationEditMutationCluster['node_spec'];

  if (instanceSettings) {
    const tserverInstanceType = instanceSettings.instanceType ?? node_spec.instance_type;
    const tserverStorageSpec = toClusterStorageSpec(node_spec.storage_spec, instanceSettings.deviceInfo);

    node_spec.instance_type = tserverInstanceType;
    node_spec.storage_spec = tserverStorageSpec;

    if (nodesAndAvailability.useDedicatedNodes) {
      node_spec.tserver = {
        ...(node_spec.tserver ?? {}),
        instance_type: tserverInstanceType,
        storage_spec: tserverStorageSpec
      };

      const masterInstanceType = instanceSettings.masterInstanceType ?? tserverInstanceType;
      const masterStorageSpec = toClusterStorageSpec(
        node_spec.master?.storage_spec ?? tserverStorageSpec,
        instanceSettings.masterDeviceInfo ?? instanceSettings.deviceInfo
      );

      node_spec.master = {
        ...(node_spec.master ?? {}),
        instance_type: masterInstanceType,
        storage_spec: masterStorageSpec
      };
    }
  }

  const partitionUuidForGeo =
    selectedPartitionUUID ??
    primaryCluster.partitions_spec?.find((p) => p.default_partition)?.uuid ??
    primaryCluster.partitions_spec?.[0]?.uuid;

  if (primaryCluster.partitions_spec?.length && partitionUuidForGeo) {
    const selectedPartition = primaryCluster.partitions_spec.find(
      (p) => p.uuid === partitionUuidForGeo
    );
    if (!selectedPartition?.placement) {
      throw new Error('Selected partition placement data is missing');
    }
    const stats = countRegionsAzsAndNodes(selectedPartition.placement);
    const resilience = mapUniversePayloadToResilienceAndRegionsProps(
      providerRegions,
      stats,
      selectedPartition
    );
    const { clusterUUID, partitionsSpec } = buildGeoPartitionPlacementEditPayload(
      universeData,
      partitionUuidForGeo,
      resilience,
      nodesAndAvailability
    );
    return {
      uuid: clusterUUID,
      num_nodes,
      node_spec,
      partitions_spec: partitionsSpec
    };
  }

  if (!primaryCluster.placement_spec) {
    throw new Error('Primary cluster placement data is missing');
  }
  const stats = countRegionsAzsAndNodes(primaryCluster.placement_spec);
  const resilience = mapUniversePayloadToResilienceAndRegionsProps(
    providerRegions,
    stats,
    primaryCluster
  );
  const { clusterUUID, placementSpec } = buildPrimaryPlacementEditPayload(
    universeData,
    resilience,
    nodesAndAvailability
  );

  return {
    uuid: clusterUUID,
    num_nodes,
    node_spec,
    placement_spec: placementSpec
  };
};
