import { describe, expect, it } from 'vitest';
import { TFunction } from 'i18next';
import {
  ClusterPlacementSpec,
  ClusterSpecClusterType,
  Universe
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../../create-universe/steps/resilence-regions/dtos';
import {
  buildGeoPartitionPlacementEditPayload,
  buildPrimaryPlacementEditPayload,
  getNodesAvailabilityDefaultsForEditPlacement
} from './EditPlacementUtils';
import { getResilientType } from '../EditUniverseUtils';

const PRIMARY = ClusterSpecClusterType.PRIMARY;

const region = {
  uuid: 'region-1',
  code: 'us-west-2',
  name: 'US West',
  zones: [
    { uuid: 'az-1', code: 'us-west-2a', name: 'us-west-2a', subnet: 'subnet-a' },
    { uuid: 'az-2', code: 'us-west-2b', name: 'us-west-2b', subnet: 'subnet-b' }
  ]
};

const resilience = {
  resilienceType: ResilienceType.REGULAR,
  resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
  faultToleranceType: FaultToleranceType.AZ_LEVEL,
  resilienceFactor: 2,
  nodeCount: 2,
  regions: [region]
} as ResilienceAndRegionsProps;

const nodesAndAvailability = {
  availabilityZones: {
    [region.code]: [{ uuid: 'az-1', name: 'us-west-2a', nodeCount: 2, preffered: 0 }]
  },
  useDedicatedNodes: false
};

const baseUniverse = {
  info: {
    universe_uuid: 'u-1'
  },
  spec: {
    clusters: [
      {
        uuid: 'primary-1',
        cluster_type: PRIMARY,
        placement_spec: {
          cloud_list: [
            {
              uuid: 'cloud-1',
              code: 'aws',
              default_region: 'region-1',
              region_list: [
                {
                  uuid: 'region-1',
                  code: 'us-west-2',
                  name: 'US West',
                  az_list: [{ uuid: 'az-1', name: 'us-west-2a', num_nodes_in_az: 1 }]
                }
              ]
            }
          ]
        },
        partitions_spec: [
          {
            uuid: 'partition-1',
            name: 'p1',
            placement: {
              cloud_list: [
                {
                  uuid: 'cloud-1',
                  code: 'aws',
                  default_region: 'region-1',
                  region_list: [
                    {
                      uuid: 'region-1',
                      code: 'us-west-2',
                      name: 'US West',
                      az_list: [{ uuid: 'az-1', name: 'us-west-2a', num_nodes_in_az: 1 }]
                    }
                  ]
                }
              ]
            }
          },
          {
            uuid: 'partition-2',
            name: 'p2',
            placement: {
              cloud_list: [
                {
                  uuid: 'cloud-1',
                  code: 'aws',
                  default_region: 'region-1',
                  region_list: [
                    {
                      uuid: 'region-1',
                      code: 'us-west-2',
                      name: 'US West',
                      az_list: [{ uuid: 'az-2', name: 'us-west-2b', num_nodes_in_az: 5 }]
                    }
                  ]
                }
              ]
            }
          }
        ]
      }
    ]
  }
} as Universe;

describe('EditPlacementUtils payload builders', () => {
  it('buildPrimaryPlacementEditPayload uses edited AZ node distribution from nodes availability', () => {
    const payload = buildPrimaryPlacementEditPayload(
      baseUniverse,
      resilience,
      nodesAndAvailability as any
    );

    const azList = payload.placementSpec.cloud_list[0].region_list?.[0].az_list ?? [];
    expect(payload.clusterUUID).toBe('primary-1');
    expect(azList).toHaveLength(1);
    expect(azList[0].num_nodes_in_az).toBe(2);
    expect(azList[0].leader_preference).toBe(1);
  });

  it('buildGeoPartitionPlacementEditPayload updates only selected partition placement', () => {
    const payload = buildGeoPartitionPlacementEditPayload(
      baseUniverse,
      'partition-1',
      resilience,
      nodesAndAvailability as any
    );

    const selectedPartition = payload.partitionsSpec.find((p) => p.uuid === 'partition-1');
    const untouchedPartition = payload.partitionsSpec.find((p) => p.uuid === 'partition-2');

    expect(payload.clusterUUID).toBe('primary-1');
    expect(selectedPartition?.placement?.cloud_list?.[0].region_list?.[0].az_list?.[0].num_nodes_in_az).toBe(2);
    expect(untouchedPartition?.placement?.cloud_list?.[0].region_list?.[0].az_list?.[0].num_nodes_in_az).toBe(
      5
    );
  });

  it('buildGeoPartitionPlacementEditPayload updates selected partition replication factor from resilience', () => {
    const guidedResilience = {
      ...resilience,
      resilienceFormMode: ResilienceFormMode.GUIDED,
      faultToleranceType: FaultToleranceType.NODE_LEVEL,
      resilienceFactor: 1
    } as ResilienceAndRegionsProps;

    const payload = buildGeoPartitionPlacementEditPayload(
      baseUniverse,
      'partition-1',
      guidedResilience,
      {
        availabilityZones: {
          [region.code]: [{ uuid: 'az-1', name: 'us-west-2a', nodeCount: 3, preffered: 0 }]
        },
        useDedicatedNodes: false
      } as any
    );

    const selectedPartition = payload.partitionsSpec.find((p) => p.uuid === 'partition-1');
    const untouchedPartition = payload.partitionsSpec.find((p) => p.uuid === 'partition-2');

    expect(selectedPartition?.replication_factor).toBe(3);
    expect(
      selectedPartition?.placement?.cloud_list?.[0].region_list?.[0].az_list?.[0].replication_factor
    ).toBe(3);
    expect(untouchedPartition?.replication_factor).toBeUndefined();
  });
});

describe('getNodesAvailabilityDefaultsForEditPlacement', () => {
  it('uses primary placement and dedicated flag for non geo-partition universes', () => {
    const universe = {
      info: {
        universe_uuid: 'u-2'
      },
      spec: {
        clusters: [
          {
            uuid: 'primary-2',
            cluster_type: PRIMARY,
            replication_factor: 5,
            node_spec: {
              dedicated_nodes: true
            },
            placement_spec: {
              cloud_list: [
                {
                  uuid: 'cloud-2',
                  code: 'aws',
                  default_region: 'region-a',
                  region_list: [
                    {
                      uuid: 'region-a',
                      code: 'us-east-1',
                      name: 'US East',
                      az_list: [{ uuid: 'az-a1', name: 'us-east-1a', num_nodes_in_az: 3 }]
                    }
                  ]
                }
              ]
            }
          }
        ]
      }
    } as Universe;

    const defaults = getNodesAvailabilityDefaultsForEditPlacement(universe);
    expect(defaults.useDedicatedNodes).toBe(true);
    expect(defaults.replicationFactor).toBe(5);
    expect(defaults.availabilityZones['us-east-1'][0].nodeCount).toBe(3);
  });

  it('uses default partition placement/rf when selected partition is not provided', () => {
    const universe = {
      info: {
        universe_uuid: 'u-3'
      },
      spec: {
        clusters: [
          {
            uuid: 'primary-3',
            cluster_type: PRIMARY,
            replication_factor: 3,
            node_spec: {
              dedicated_nodes: true
            },
            placement_spec: {
              cloud_list: [
                {
                  uuid: 'cloud-3',
                  code: 'aws',
                  default_region: 'region-b',
                  region_list: [
                    {
                      uuid: 'region-b',
                      code: 'us-west-1',
                      name: 'US West',
                      az_list: [{ uuid: 'az-b1', name: 'us-west-1a', num_nodes_in_az: 1 }]
                    }
                  ]
                }
              ]
            },
            partitions_spec: [
              {
                uuid: 'partition-a',
                name: 'p-a',
                replication_factor: 5,
                placement: {
                  cloud_list: [
                    {
                      uuid: 'cloud-3',
                      code: 'aws',
                      default_region: 'region-b',
                      region_list: [
                        {
                          uuid: 'region-b',
                          code: 'us-west-1',
                          name: 'US West',
                          az_list: [
                            { uuid: 'az-b2', name: 'us-west-1b', num_nodes_in_az: 2 }
                          ]
                        }
                      ]
                    }
                  ]
                }
              },
              {
                uuid: 'partition-default',
                name: 'p-default',
                default_partition: true,
                replication_factor: 7,
                placement: {
                  cloud_list: [
                    {
                      uuid: 'cloud-3',
                      code: 'aws',
                      default_region: 'region-b',
                      region_list: [
                        {
                          uuid: 'region-b',
                          code: 'us-west-1',
                          name: 'US West',
                          az_list: [
                            { uuid: 'az-b3', name: 'us-west-1c', num_nodes_in_az: 4 }
                          ]
                        }
                      ]
                    }
                  ]
                }
              }
            ]
          }
        ]
      }
    } as Universe;

    const defaults = getNodesAvailabilityDefaultsForEditPlacement(universe);
    expect(defaults.useDedicatedNodes).toBe(true);
    expect(defaults.replicationFactor).toBe(7);
    expect(defaults.availabilityZones['us-west-1'][0]).toMatchObject({
      uuid: 'az-b3',
      nodeCount: 4
    });
  });

  it('uses selected partition placement/rf when selected partition is provided', () => {
    const universe = {
      info: {
        universe_uuid: 'u-4'
      },
      spec: {
        clusters: [
          {
            uuid: 'primary-4',
            cluster_type: PRIMARY,
            replication_factor: 3,
            node_spec: {
              dedicated_nodes: false
            },
            placement_spec: {
              cloud_list: [
                {
                  uuid: 'cloud-4',
                  code: 'aws',
                  default_region: 'region-c',
                  region_list: [
                    {
                      uuid: 'region-c',
                      code: 'eu-central-1',
                      name: 'EU Central',
                      az_list: [{ uuid: 'az-c1', name: 'eu-central-1a', num_nodes_in_az: 1 }]
                    }
                  ]
                }
              ]
            },
            partitions_spec: [
              {
                uuid: 'partition-a',
                name: 'p-a',
                replication_factor: 6,
                placement: {
                  cloud_list: [
                    {
                      uuid: 'cloud-4',
                      code: 'aws',
                      default_region: 'region-c',
                      region_list: [
                        {
                          uuid: 'region-c',
                          code: 'eu-central-1',
                          name: 'EU Central',
                          az_list: [
                            { uuid: 'az-c2', name: 'eu-central-1b', num_nodes_in_az: 5 }
                          ]
                        }
                      ]
                    }
                  ]
                }
              },
              {
                uuid: 'partition-default',
                name: 'p-default',
                default_partition: true,
                replication_factor: 7,
                placement: {
                  cloud_list: [
                    {
                      uuid: 'cloud-4',
                      code: 'aws',
                      default_region: 'region-c',
                      region_list: [
                        {
                          uuid: 'region-c',
                          code: 'eu-central-1',
                          name: 'EU Central',
                          az_list: [
                            { uuid: 'az-c3', name: 'eu-central-1c', num_nodes_in_az: 3 }
                          ]
                        }
                      ]
                    }
                  ]
                }
              }
            ]
          }
        ]
      }
    } as Universe;

    const defaults = getNodesAvailabilityDefaultsForEditPlacement(universe, 'partition-a');
    expect(defaults.useDedicatedNodes).toBe(false);
    expect(defaults.replicationFactor).toBe(6);
    expect(defaults.availabilityZones['eu-central-1'][0]).toMatchObject({
      uuid: 'az-c2',
      nodeCount: 5
    });
  });
});

describe('getResilientType', () => {
  const t = ((key: string, options?: Record<string, unknown>) => {
    if (key === 'notResilient') return 'Not Resilient to outages';
    return `${key}:${options?.count as number}`;
  }) as unknown as TFunction;

  const makePlacementSpec = (regionAzNodeLayout: number[][]): ClusterPlacementSpec =>
    ({
    cloud_list: [
      {
        uuid: 'cloud-1',
        code: 'aws',
        default_region: 'region-1',
        region_list: regionAzNodeLayout.map((azNodeCounts, regionIndex) => ({
          uuid: `region-${regionIndex + 1}`,
          code: `r${regionIndex + 1}`,
          name: `Region ${regionIndex + 1}`,
          az_list: azNodeCounts.map((nodeCount, azIndex) => ({
            uuid: `r${regionIndex + 1}-az-${azIndex + 1}`,
            code: `r${regionIndex + 1}az${azIndex + 1}`,
            name: `r${regionIndex + 1}az${azIndex + 1}`,
            num_nodes_in_az: nodeCount
          }))
        }))
      }
    ]
  } as unknown as ClusterPlacementSpec);

  it('infers region-level resilience when regions equal RF', () => {
    const placementSpec = makePlacementSpec([[1], [1], [1]]);
    expect(getResilientType(placementSpec, 3, t)).toBe('regionResilient:1');
  });

  it('infers az-level resilience when regions are fewer than RF and AZ count equals RF', () => {
    const placementSpec = makePlacementSpec([[1, 1], [1]]);
    expect(getResilientType(placementSpec, 3, t)).toBe('azResilient:1');
  });

  it('infers node-level resilience when regions and AZs are fewer than RF', () => {
    const placementSpec = makePlacementSpec([[2, 3]]);
    expect(getResilientType(placementSpec, 3, t)).toBe('nodeResilient:1');
  });

  it('returns not resilient when RF is 1', () => {
    const placementSpec = makePlacementSpec([[1]]);
    expect(getResilientType(placementSpec, 1, t)).toBe('Not Resilient to outages');
  });

  it('returns not resilient for uninferable placement layout', () => {
    const placementSpec = makePlacementSpec([[1, 1, 1]]);
    expect(getResilientType(placementSpec, 2, t)).toBe('Not Resilient to outages');
  });
});
