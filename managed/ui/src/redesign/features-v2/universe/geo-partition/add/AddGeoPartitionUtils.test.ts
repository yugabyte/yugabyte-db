import { describe, expect, it } from 'vitest';
import {
  ClusterSpecClusterType,
  UniverseRespResponse
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  buildUniverseSpecForGeoPartitionPricing,
  computeIsNewGeoPartitionFromUniverse,
  getDefaultPrimaryPartitionSpec,
  getExistingGeoPartitions,
  getNextGeoPartitionDisplayNumber,
  hasConfiguredAvailabilityZones,
  prepareAddGeoPartitionPayload,
  sumNumNodesInClusterPartitionPlacement
} from './AddGeoPartitionUtils';
import { AddGeoPartitionContextProps, GeoPartition } from './AddGeoPartitionContext';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../../create-universe/steps/resilence-regions/dtos';

const PRIMARY = ClusterSpecClusterType.PRIMARY;

function primaryCluster(overrides: Record<string, unknown> = {}) {
  return {
    cluster_type: PRIMARY,
    replication_factor: 3,
    provider_spec: { provider: 'provider-uuid-1' },
    placement_spec: {
      cloud_list: [
        {
          code: 'aws',
          uuid: 'cloud-uuid-1',
          region_list: [
            {
              uuid: 'region-uuid-1',
              code: 'us-west-2',
              name: 'US West',
              az_list: [
                { uuid: 'az-uuid-1', name: 'az-a', num_nodes_in_az: 1 },
                { uuid: 'az-uuid-2', name: 'az-b', num_nodes_in_az: 1 }
              ]
            }
          ]
        }
      ]
    },
    ...overrides
  };
}

function minimalUniverse(
  overrides: { clusters?: unknown[]; info?: Record<string, unknown> } = {}
): UniverseRespResponse {
  return {
    info: { universe_uuid: 'universe-1', arch: 'x86_64', ...overrides.info } as any,
    spec: {
      clusters: overrides.clusters ?? [primaryCluster()]
    } as any
  } as UniverseRespResponse;
}

const sampleRegion = {
  uuid: 'region-uuid-1',
  code: 'us-west-2',
  name: 'US West',
  zones: [
    { uuid: 'az-uuid-1', code: 'us-west-2a', name: 'az-a', subnet: 's1' },
    { uuid: 'az-uuid-2', code: 'us-west-2b', name: 'az-b', subnet: 's2' }
  ]
};

/** Defaults to expert mode so resilienceFactor is literal API replication factor (matches create-universe expert semantics). */
function resilienceProps(
  overrides: Partial<{
    regions: typeof sampleRegion[];
    resilienceFactor: number;
    faultToleranceType: FaultToleranceType;
    resilienceFormMode: ResilienceFormMode;
  }> = {}
): ResilienceAndRegionsProps {
  return {
    resilienceType: ResilienceType.REGULAR,
    resilienceFormMode: overrides.resilienceFormMode ?? ResilienceFormMode.EXPERT_MODE,
    regions: (overrides.regions ?? [sampleRegion]) as ResilienceAndRegionsProps['regions'],
    resilienceFactor: overrides.resilienceFactor ?? 3,
    faultToleranceType: overrides.faultToleranceType ?? FaultToleranceType.AZ_LEVEL,
    nodeCount: 1
  };
}

function nodesAzForRegion(regionCode: string) {
  return {
    availabilityZones: {
      [regionCode]: [
        { name: 'az-a', uuid: 'az-uuid-1', nodeCount: 1, preffered: 0 },
        { name: 'az-b', uuid: 'az-uuid-2', nodeCount: 2, preffered: 1 }
      ]
    },
    useDedicatedNodes: false
  };
}

describe('AddGeoPartitionUtils', () => {
  describe('getDefaultPrimaryPartitionSpec', () => {
    it('returns first partition when primary partitions_spec[0].name is default', () => {
      const defaultPart = {
        name: 'default',
        uuid: 'part-default',
        tablespace_name: 'ts_default',
        placement: { cloud_list: [] }
      };
      const u = minimalUniverse({
        clusters: [
          primaryCluster({
            partitions_spec: [defaultPart]
          })
        ]
      });
      expect(getDefaultPrimaryPartitionSpec(u)).toEqual(defaultPart);
    });

    it('returns null when first partition is not named default', () => {
      const u = minimalUniverse({
        clusters: [
          primaryCluster({
            partitions_spec: [{ name: 'other', placement: { cloud_list: [] } }]
          })
        ]
      });
      expect(getDefaultPrimaryPartitionSpec(u)).toBeNull();
    });

    it('returns null when partitions_spec is empty', () => {
      const u = minimalUniverse({
        clusters: [primaryCluster({ partitions_spec: [] })]
      });
      expect(getDefaultPrimaryPartitionSpec(u)).toBeNull();
    });

    it('returns null when partitions_spec is missing', () => {
      const u = minimalUniverse({
        clusters: [primaryCluster({ partitions_spec: undefined })]
      });
      expect(getDefaultPrimaryPartitionSpec(u)).toBeNull();
    });

    it('returns null when there is no primary cluster', () => {
      const u = minimalUniverse({
        clusters: [
          {
            cluster_type: ClusterSpecClusterType.ASYNC,
            partitions_spec: [{ name: 'default', placement: { cloud_list: [] } }]
          }
        ]
      });
      expect(getDefaultPrimaryPartitionSpec(u)).toBeNull();
    });
  });

  const minimalPart = (name: string) => ({
    name,
    placement: { cloud_list: [] }
  });

  describe('computeIsNewGeoPartitionFromUniverse', () => {
    const primaryUuid = 'primary-cluster-uuid';

    it('returns true when geo_partitioned is false regardless of partition count', () => {
      const u = minimalUniverse({
        info: {
          clusters: [{ uuid: primaryUuid, geo_partitioned: false }]
        },
        clusters: [
          primaryCluster({
            uuid: primaryUuid,
            partitions_spec: [minimalPart('a'), minimalPart('b'), minimalPart('c')]
          })
        ]
      });
      expect(computeIsNewGeoPartitionFromUniverse(u)).toBe(true);
    });

    it('returns true when geo_partitioned is true and primary has one partition', () => {
      const u = minimalUniverse({
        info: {
          clusters: [{ uuid: primaryUuid, geo_partitioned: true }]
        },
        clusters: [
          primaryCluster({
            uuid: primaryUuid,
            partitions_spec: [minimalPart('only')]
          })
        ]
      });
      expect(computeIsNewGeoPartitionFromUniverse(u)).toBe(true);
    });

    it('returns false when geo_partitioned is true and primary has more than one partition', () => {
      const u = minimalUniverse({
        info: {
          clusters: [{ uuid: primaryUuid, geo_partitioned: true }]
        },
        clusters: [
          primaryCluster({
            uuid: primaryUuid,
            partitions_spec: [minimalPart('p1'), minimalPart('p2')]
          })
        ]
      });
      expect(computeIsNewGeoPartitionFromUniverse(u)).toBe(false);
    });

    it('returns false when universeData is undefined', () => {
      expect(computeIsNewGeoPartitionFromUniverse(undefined)).toBe(false);
    });

    it('returns false when info is missing', () => {
      const u = { spec: { clusters: [primaryCluster({ uuid: primaryUuid })] } } as UniverseRespResponse;
      expect(computeIsNewGeoPartitionFromUniverse(u)).toBe(false);
    });

    it('returns false when there is no primary cluster', () => {
      const u = minimalUniverse({
        info: {
          clusters: [{ uuid: 'async-1', geo_partitioned: true }]
        },
        clusters: [
          {
            cluster_type: ClusterSpecClusterType.ASYNC,
            uuid: 'async-1',
            partitions_spec: [minimalPart('a'), minimalPart('b')]
          } as any
        ]
      });
      expect(computeIsNewGeoPartitionFromUniverse(u)).toBe(false);
    });
  });

  describe('getExistingGeoPartitions', () => {
    it('returns empty array when partitions_spec is missing', () => {
      expect(getExistingGeoPartitions(minimalUniverse())).toEqual([]);
    });

    it('flattens partitions_spec from all clusters', () => {
      const p1 = { name: 'p1', placement: { cloud_list: [] } };
      const p2 = { name: 'p2', placement: { cloud_list: [] } };
      const u = minimalUniverse({
        clusters: [
          primaryCluster({ partitions_spec: [p1] }),
          {
            cluster_type: ClusterSpecClusterType.ASYNC,
            partitions_spec: [p2]
          } as any
        ]
      });
      expect(getExistingGeoPartitions(u)).toEqual([p1, p2]);
    });
  });

  describe('hasConfiguredAvailabilityZones', () => {
    it('returns false for undefined and empty object', () => {
      expect(hasConfiguredAvailabilityZones(undefined)).toBe(false);
      expect(hasConfiguredAvailabilityZones({})).toBe(false);
    });

    it('returns true when at least one region key exists', () => {
      expect(hasConfiguredAvailabilityZones({ 'us-west-2': [] })).toBe(true);
    });
  });

  describe('getNextGeoPartitionDisplayNumber', () => {
    it('when isNewGeoPartition: adds existing and wizard counts only', () => {
      expect(getNextGeoPartitionDisplayNumber(true, 0, 1)).toBe(1);
      expect(getNextGeoPartitionDisplayNumber(true, 2, 2)).toBe(4);
    });

    it('when not new default flow: adds 1 to the sum', () => {
      expect(getNextGeoPartitionDisplayNumber(false, 0, 1)).toBe(2);
      expect(getNextGeoPartitionDisplayNumber(false, 2, 2)).toBe(5);
    });
  });

  describe('sumNumNodesInClusterPartitionPlacement', () => {
    it('sums num_nodes_in_az across placement', () => {
      const spec = {
        placement: {
          cloud_list: [
            {
              region_list: [
                {
                  az_list: [
                    { num_nodes_in_az: 2 },
                    { num_nodes_in_az: 3 }
                  ]
                }
              ]
            }
          ]
        }
      } as any;
      expect(sumNumNodesInClusterPartitionPlacement(spec)).toBe(5);
    });
  });

  describe('buildUniverseSpecForGeoPartitionPricing', () => {
    it('sets primary placement_spec and single partitions_spec entry', () => {
      const u = minimalUniverse();
      const geoSpec = {
        name: 'priced-part',
        tablespace_name: 'ts',
        placement: {
          cloud_list: [
            {
              uuid: 'provider-uuid-1',
              code: 'aws',
              region_list: []
            }
          ]
        }
      } as any;
      const result = buildUniverseSpecForGeoPartitionPricing(u, geoSpec);
      expect(result).toBeDefined();
      const primary = result!.spec.clusters.find((c: any) => c.cluster_type === PRIMARY);
      expect(primary!.placement_spec).toEqual(geoSpec.placement);
      expect(primary!.partitions_spec).toEqual([geoSpec]);
    });

    it('returns undefined without spec', () => {
      expect(
        buildUniverseSpecForGeoPartitionPricing({} as UniverseRespResponse, {} as any)
      ).toBeUndefined();
    });
  });

  describe('prepareAddGeoPartitionPayload', () => {
    const baseContext = (gp: GeoPartition[], opts: Partial<AddGeoPartitionContextProps> = {}) =>
      ({
        geoPartitions: gp,
        activeGeoPartitionIndex: 0,
        activeStep: 1,
        isNewGeoPartition: false,
        universeData: minimalUniverse(),
        ...opts
      }) as AddGeoPartitionContextProps;

    it('returns empty array when geoPartitions is empty', () => {
      expect(prepareAddGeoPartitionPayload(baseContext([]))).toEqual([]);
    });

    it('throws when universe cluster data is missing', () => {
      expect(() =>
        prepareAddGeoPartitionPayload({
          ...baseContext([]),
          universeData: { spec: {} } as UniverseRespResponse
        })
      ).toThrow('Universe cluster data is missing');
    });

    it('throws when primary cluster is missing', () => {
      expect(() =>
        prepareAddGeoPartitionPayload({
          ...baseContext([]),
          universeData: minimalUniverse({
            clusters: [
              {
                cluster_type: ClusterSpecClusterType.ASYNC,
                provider_spec: { provider: 'p' },
                placement_spec: primaryCluster().placement_spec
              } as any
            ]
          })
        })
      ).toThrow('Primary cluster not found');
    });

    it('throws when provider UUID is missing', () => {
      expect(() =>
        prepareAddGeoPartitionPayload({
          ...baseContext([]),
          universeData: minimalUniverse({
            clusters: [primaryCluster({ provider_spec: {} })]
          })
        })
      ).toThrow('Provider UUID is missing');
    });

    it('throws when primary placement cloud code is missing', () => {
      expect(() =>
        prepareAddGeoPartitionPayload({
          ...baseContext([]),
          universeData: minimalUniverse({
            clusters: [
              primaryCluster({
                placement_spec: { cloud_list: [{ uuid: 'c', region_list: [] }] }
              })
            ]
          })
        })
      ).toThrow('Primary cluster placement cloud is missing');
    });

    it('throws when resilience is missing', () => {
      const gp: GeoPartition = {
        name: 'g1',
        tablespaceName: 't1',
        nodesAndAvailability: { availabilityZones: {}, useDedicatedNodes: false }
      };
      expect(() => prepareAddGeoPartitionPayload(baseContext([gp]))).toThrow(
        'Resilience data is missing'
      );
    });

    it('throws when availability zones object is missing', () => {
      const gp: GeoPartition = {
        name: 'g1',
        tablespaceName: 't1',
        resilience: resilienceProps(),
        nodesAndAvailability: { useDedicatedNodes: false } as any
      };
      expect(() => prepareAddGeoPartitionPayload(baseContext([gp]))).toThrow(
        'Availability zones data is missing'
      );
    });

    it('default row uses primary placement and RF when isNewGeoPartition and empty AZ map', () => {
      const gp: GeoPartition = {
        name: 'default_display',
        tablespaceName: 'ts1',
        resilience: resilienceProps(),
        nodesAndAvailability: { availabilityZones: {}, useDedicatedNodes: false }
      };
      const payload = prepareAddGeoPartitionPayload(
        baseContext([gp], { isNewGeoPartition: true })
      );
      expect(payload).toHaveLength(1);
      expect(payload[0].default_partition).toBe(true);
      expect(payload[0].replication_factor).toBe(3);
      expect(payload[0].placement).toEqual(primaryCluster().placement_spec);
    });

    it('default row uses getPlacementRegions when AZ map is configured', () => {
      const gp: GeoPartition = {
        name: 'default_display',
        tablespaceName: 'ts1',
        resilience: resilienceProps(),
        nodesAndAvailability: nodesAzForRegion('us-west-2')
      };
      const payload = prepareAddGeoPartitionPayload(
        baseContext([gp], { isNewGeoPartition: true })
      );
      expect(payload[0].default_partition).toBe(true);
      expect(payload[0].placement?.cloud_list?.[0].region_list?.length).toBe(1);
      expect(payload[0].placement?.cloud_list?.[0].region_list?.[0].uuid).toBe('region-uuid-1');
    });

    it('non-default row uses partition RF and default_partition false', () => {
      const gp: GeoPartition = {
        name: 'geo-east',
        tablespaceName: 'ts_east',
        resilience: resilienceProps({ resilienceFactor: 3 }),
        nodesAndAvailability: nodesAzForRegion('us-west-2')
      };
      const payload = prepareAddGeoPartitionPayload(baseContext([gp], { isNewGeoPartition: false }));
      expect(payload[0].default_partition).toBe(false);
      expect(payload[0].replication_factor).toBe(3);
    });

    it('guided FT=1 maps to API replication_factor 3 for non-default row', () => {
      const gp: GeoPartition = {
        name: 'geo-guided',
        tablespaceName: 'ts_g',
        resilience: resilienceProps({
          resilienceFormMode: ResilienceFormMode.GUIDED,
          resilienceFactor: 1,
          faultToleranceType: FaultToleranceType.AZ_LEVEL
        }),
        nodesAndAvailability: nodesAzForRegion('us-west-2')
      };
      const payload = prepareAddGeoPartitionPayload(baseContext([gp], { isNewGeoPartition: false }));
      expect(payload[0].replication_factor).toBe(3);
      const azList = payload[0].placement?.cloud_list?.[0].region_list?.[0].az_list ?? [];
      const sumRf = azList.reduce((s, az) => s + (az.replication_factor ?? 0), 0);
      expect(sumRf).toBe(3);
    });

    it('multiple wizard partitions produce distinct names and tablespaces', () => {
      const gp0: GeoPartition = {
        name: 'Primary Row',
        tablespaceName: 'ts0',
        resilience: resilienceProps(),
        nodesAndAvailability: { availabilityZones: {}, useDedicatedNodes: false }
      };
      const gp1: GeoPartition = {
        name: 'Geo Partition 2',
        tablespaceName: 'ts1',
        resilience: resilienceProps(),
        nodesAndAvailability: nodesAzForRegion('us-west-2')
      };
      const payload = prepareAddGeoPartitionPayload(
        baseContext([gp0, gp1], { isNewGeoPartition: true })
      );
      expect(payload).toHaveLength(2);
      expect(payload[0].name).toBe('Primary Row');
      expect(payload[0].default_partition).toBe(true);
      expect(payload[1].name).toBe('Geo Partition 2');
      expect(payload[1].default_partition).toBe(false);
      expect(payload[1].tablespace_name).toBe('ts1');
    });
  });
});
