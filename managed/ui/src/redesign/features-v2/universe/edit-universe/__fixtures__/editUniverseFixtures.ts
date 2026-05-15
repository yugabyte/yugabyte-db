import type { Region } from '@app/redesign/helpers/dtos';
import type { ClusterPlacementSpec } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  ClusterSpecClusterType,
  NodeDetailsDedicatedTo,
  Universe
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

export const FIXTURE_PROVIDER_UUID = 'provider-uuid-1';
export const FIXTURE_CLOUD_UUID = 'cloud-uuid-1';
export const FIXTURE_REGION_UUID = 'region-uuid-1';
export const FIXTURE_REGION_CODE = 'us-west-2';
export const FIXTURE_AZ_1_UUID = 'az-uuid-1';
export const FIXTURE_AZ_2_UUID = 'az-uuid-2';
export const FIXTURE_PRIMARY_CLUSTER_UUID = 'primary-cluster-uuid';
export const FIXTURE_ASYNC_CLUSTER_UUID = 'async-cluster-uuid';

const PRIMARY = ClusterSpecClusterType.PRIMARY;
const ASYNC = ClusterSpecClusterType.ASYNC;

export function makeProviderRegions(): Region[] {
  return [
    {
      uuid: FIXTURE_REGION_UUID,
      code: FIXTURE_REGION_CODE,
      name: 'US West',
      ybImage: '',
      longitude: 0,
      latitude: 0,
      active: true,
      securityGroupId: null,
      details: null,
      zones: [
        {
          uuid: FIXTURE_AZ_1_UUID,
          code: 'us-west-2a',
          name: 'az-a',
          active: true,
          subnet: 's1'
        },
        {
          uuid: FIXTURE_AZ_2_UUID,
          code: 'us-west-2b',
          name: 'az-b',
          active: true,
          subnet: 's2'
        }
      ]
    }
  ];
}

export function makePrimaryPlacementSpec(): ClusterPlacementSpec {
  return {
    cloud_list: [
      {
        code: 'aws',
        uuid: FIXTURE_CLOUD_UUID,
        default_region: FIXTURE_REGION_UUID,
        region_list: [
          {
            uuid: FIXTURE_REGION_UUID,
            code: FIXTURE_REGION_CODE,
            name: 'US West',
            az_list: [
              {
                uuid: FIXTURE_AZ_1_UUID,
                name: 'us-west-2a',
                num_nodes_in_az: 1,
                replication_factor: 1
              },
              {
                uuid: FIXTURE_AZ_2_UUID,
                name: 'us-west-2b',
                num_nodes_in_az: 1,
                replication_factor: 1
              }
            ]
          }
        ]
      }
    ]
  };
}

function baseNodeSpec() {
  return {
    instance_type: 'c5.xlarge',
    dedicated_nodes: false,
    storage_spec: {
      volume_size: 100,
      num_volumes: 1,
      storage_type: 'GP3',
      disk_iops: 3000,
      throughput: 125
    }
  };
}

function primaryCluster(overrides: Record<string, unknown> = {}) {
  return {
    uuid: FIXTURE_PRIMARY_CLUSTER_UUID,
    cluster_type: PRIMARY,
    replication_factor: 3,
    num_nodes: 2,
    provider_spec: { provider: FIXTURE_PROVIDER_UUID },
    placement_spec: makePrimaryPlacementSpec(),
    node_spec: baseNodeSpec(),
    ...overrides
  };
}

/** Non–geo-partitioned universe: primary only, no `partitions_spec`. */
export function makeNonGeoUniverse(): Universe {
  return {
    info: {
      universe_uuid: 'universe-non-geo',
      arch: 'x86_64',
      node_details_set: []
    },
    spec: {
      name: 'test-universe',
      clusters: [primaryCluster()]
    }
  } as unknown as Universe;
}

/** Same as non-geo but `info.node_details_set` marks dedicated T-Server / Master nodes (drives HardwareTab). */
export function makeNonGeoUniverseWithDedicatedNodeDetails(): Universe {
  const u = makeNonGeoUniverse();
  return {
    ...u,
    info: {
      ...u.info,
      node_details_set: [
        {
          node_uuid: 'n1',
          az_uuid: FIXTURE_AZ_1_UUID,
          dedicated_to: NodeDetailsDedicatedTo.TSERVER
        },
        {
          node_uuid: 'n2',
          az_uuid: FIXTURE_AZ_1_UUID,
          dedicated_to: NodeDetailsDedicatedTo.MASTER
        },
        {
          node_uuid: 'n3',
          az_uuid: FIXTURE_AZ_2_UUID,
          dedicated_to: NodeDetailsDedicatedTo.TSERVER
        }
      ]
    }
  } as unknown as Universe;
}

function rrPlacementSlice() {
  return {
    cloud_list: [
      {
        code: 'aws',
        uuid: FIXTURE_CLOUD_UUID,
        default_region: FIXTURE_REGION_UUID,
        region_list: [
          {
            uuid: FIXTURE_REGION_UUID,
            code: FIXTURE_REGION_CODE,
            name: 'US West',
            az_list: [
              {
                uuid: FIXTURE_AZ_1_UUID,
                name: 'us-west-2a',
                num_nodes_in_az: 1,
                replication_factor: 1
              }
            ]
          }
        ]
      }
    ]
  };
}

/** Read replica with cluster-level `placement_spec` only (no `partitions_spec`). */
export function makeReadReplicaClusterPlacementSpecOnly() {
  return {
    uuid: FIXTURE_ASYNC_CLUSTER_UUID,
    cluster_type: ASYNC,
    replication_factor: 1,
    num_nodes: 1,
    provider_spec: { provider: FIXTURE_PROVIDER_UUID },
    placement_spec: rrPlacementSlice(),
    node_spec: {
      instance_type: 't3.medium',
      dedicated_nodes: false,
      storage_spec: {
        volume_size: 50,
        num_volumes: 1,
        storage_type: 'GP3',
        disk_iops: 3000,
        throughput: 125
      }
    }
  };
}

/** Read replica with two geo-style partitions (each with placement). */
export function makeReadReplicaClusterWithPartitionsSpec() {
  return {
    uuid: FIXTURE_ASYNC_CLUSTER_UUID,
    cluster_type: ASYNC,
    replication_factor: 1,
    num_nodes: 2,
    provider_spec: { provider: FIXTURE_PROVIDER_UUID },
    placement_spec: rrPlacementSlice(),
    partitions_spec: [
      {
        uuid: 'rr-part-1',
        name: 'rr-p1',
        placement: rrPlacementSlice() as ClusterPlacementSpec
      },
      {
        uuid: 'rr-part-2',
        name: 'rr-p2',
        placement: {
          ...rrPlacementSlice(),
          cloud_list: [
            {
              ...rrPlacementSlice().cloud_list[0],
              region_list: [
                {
                  uuid: FIXTURE_REGION_UUID,
                  code: FIXTURE_REGION_CODE,
                  name: 'US West',
                  az_list: [
                    {
                      uuid: FIXTURE_AZ_2_UUID,
                      name: 'us-west-2b',
                      num_nodes_in_az: 1,
                      replication_factor: 1
                    }
                  ]
                }
              ]
            }
          ]
        } as ClusterPlacementSpec
      }
    ]
  };
}

export function makeNonGeoUniverseWithReadReplicaPlacementSpec(): Universe {
  return {
    ...makeNonGeoUniverse(),
    spec: {
      ...makeNonGeoUniverse().spec,
      clusters: [primaryCluster(), makeReadReplicaClusterPlacementSpecOnly()]
    }
  } as unknown as Universe;
}

export function makeNonGeoUniverseWithReadReplicaPartitions(): Universe {
  return {
    ...makeNonGeoUniverse(),
    spec: {
      ...makeNonGeoUniverse().spec,
      clusters: [primaryCluster(), makeReadReplicaClusterWithPartitionsSpec()]
    }
  } as unknown as Universe;
}

function partitionPlacement(azUuid: string, numNodes: number): ClusterPlacementSpec {
  return {
    cloud_list: [
      {
        code: 'aws',
        uuid: FIXTURE_CLOUD_UUID,
        default_region: FIXTURE_REGION_UUID,
        region_list: [
          {
            uuid: FIXTURE_REGION_UUID,
            code: FIXTURE_REGION_CODE,
            name: 'US West',
            az_list: [
              {
                uuid: azUuid,
                name: 'us-west-2a',
                num_nodes_in_az: numNodes,
                replication_factor: 1
              }
            ]
          }
        ]
      }
    ]
  };
}

/** Geo-partitioned primary: two partitions on PRIMARY cluster. */
export function makeGeoUniverse(): Universe {
  return {
    info: {
      universe_uuid: 'universe-geo',
      arch: 'x86_64',
      node_details_set: []
    },
    spec: {
      name: 'geo-universe',
      clusters: [
        primaryCluster({
          partitions_spec: [
            {
              uuid: 'geo-part-1',
              name: 'partition-us',
              placement: partitionPlacement(FIXTURE_AZ_1_UUID, 1)
            },
            {
              uuid: 'geo-part-2',
              name: 'partition-eu',
              placement: partitionPlacement(FIXTURE_AZ_2_UUID, 2)
            }
          ]
        })
      ]
    }
  } as unknown as Universe;
}

export function makeGeoUniverseWithDedicatedNodeDetails(): Universe {
  const u = makeGeoUniverse();
  return {
    ...u,
    info: {
      ...u.info,
      node_details_set: [
        { node_uuid: 'g1', az_uuid: FIXTURE_AZ_1_UUID, dedicated_to: NodeDetailsDedicatedTo.TSERVER },
        { node_uuid: 'g2', az_uuid: FIXTURE_AZ_2_UUID, dedicated_to: NodeDetailsDedicatedTo.MASTER }
      ]
    }
  } as unknown as Universe;
}

export function makeGeoUniverseWithReadReplicaPartitions(): Universe {
  const u = makeGeoUniverse();
  return {
    ...u,
    spec: {
      ...u.spec!,
      clusters: [...(u.spec!.clusters as unknown[]), makeReadReplicaClusterWithPartitionsSpec()]
    }
  } as unknown as Universe;
}

/** Geo primary + RR that only has `placement_spec` (GeoPartitionPlacementView currently omits RR cards). */
export function makeGeoUniverseWithReadReplicaPlacementSpecOnly(): Universe {
  const u = makeGeoUniverse();
  return {
    ...u,
    spec: {
      ...u.spec!,
      clusters: [...(u.spec!.clusters as unknown[]), makeReadReplicaClusterPlacementSpecOnly()]
    }
  } as unknown as Universe;
}
