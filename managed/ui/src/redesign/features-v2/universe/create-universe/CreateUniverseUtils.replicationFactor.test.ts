import { describe, expect, it } from 'vitest';
import {
  distributeReplicationFactorAcrossAzs,
  getEffectiveReplicationFactorForResilience,
  getPlacementRegions
} from './CreateUniverseUtils';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from './steps/resilence-regions/dtos';

const sampleRegions: ResilienceAndRegionsProps['regions'] = [
  {
    uuid: 'region-uuid-1',
    code: 'us-west-2',
    name: 'US West',
    zones: [
      { uuid: 'az-uuid-1', code: 'us-west-2a', name: 'az-a', subnet: 's1' },
      { uuid: 'az-uuid-2', code: 'us-west-2b', name: 'az-b', subnet: 's2' },
      { uuid: 'az-uuid-3', code: 'us-west-2c', name: 'az-c', subnet: 's3' }
    ]
  } as ResilienceAndRegionsProps['regions'][number]
];

function baseResilience(
  overrides: Partial<ResilienceAndRegionsProps> = {}
): ResilienceAndRegionsProps {
  return {
    resilienceType: ResilienceType.REGULAR,
    resilienceFormMode: ResilienceFormMode.GUIDED,
    regions: sampleRegions,
    resilienceFactor: 1,
    faultToleranceType: FaultToleranceType.AZ_LEVEL,
    nodeCount: 1,
    ...overrides
  };
}

describe('getEffectiveReplicationFactorForResilience', () => {
  it('expert mode uses replicationFactor from nodes availability', () => {
    expect(
      getEffectiveReplicationFactorForResilience(
        baseResilience({
          resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
          resilienceFactor: 3
        }),
        { replicationFactor: 5 }
      )
    ).toBe(5);
  });

  it('expert mode falls back to resilienceFactor when nodes RF is unset', () => {
    expect(
      getEffectiveReplicationFactorForResilience(
        baseResilience({
          resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
          resilienceFactor: 5
        })
      )
    ).toBe(5);
  });

  it('guided AZ_LEVEL FT 1 → RF 3', () => {
    expect(
      getEffectiveReplicationFactorForResilience(
        baseResilience({
          resilienceFactor: 1,
          faultToleranceType: FaultToleranceType.AZ_LEVEL
        })
      )
    ).toBe(3);
  });

  it('guided FT 2 → RF 5', () => {
    expect(
      getEffectiveReplicationFactorForResilience(
        baseResilience({
          resilienceFactor: 2,
          faultToleranceType: FaultToleranceType.REGION_LEVEL
        })
      )
    ).toBe(5);
  });

  it('guided NONE keeps stored value', () => {
    expect(
      getEffectiveReplicationFactorForResilience(
        baseResilience({
          resilienceFactor: 1,
          faultToleranceType: FaultToleranceType.NONE
        })
      )
    ).toBe(1);
  });

  it('SINGLE_NODE → 1', () => {
    expect(
      getEffectiveReplicationFactorForResilience(
        baseResilience({
          resilienceType: ResilienceType.SINGLE_NODE,
          resilienceFactor: 99
        })
      )
    ).toBe(1);
  });
});

function getAzPlacementPairs(
  regionList: ReturnType<typeof getPlacementRegions>,
  availabilityZones: Record<string, { nodeCount: number }[]>
) {
  const flatNodeCounts = Object.values(availabilityZones)
    .flat()
    .map((az) => az.nodeCount);
  const flatReplicationFactors = regionList
    .flatMap((region) => region.az_list ?? [])
    .map((az) => az.replication_factor ?? 0);
  return flatNodeCounts.map((nodeCount, index) => ({
    nodeCount,
    replicationFactor: flatReplicationFactors[index]
  }));
}

function expectValidPerAzReplication(
  regionList: ReturnType<typeof getPlacementRegions>,
  availabilityZones: Record<string, { nodeCount: number }[]>,
  targetRf: number
) {
  const pairs = getAzPlacementPairs(regionList, availabilityZones);
  const sum = pairs.reduce((total, pair) => total + pair.replicationFactor, 0);
  expect(sum).toBe(targetRf);
  pairs.forEach((pair) => {
    expect(pair.replicationFactor).toBeLessThanOrEqual(pair.nodeCount);
  });
  return pairs.map((pair) => pair.replicationFactor);
}

describe('getPlacementRegions effective RF (guided)', () => {
  it('sums per-AZ replication_factor to guided effective RF (FT 1 → 3 across 3 AZs)', () => {
    const resilience = baseResilience({
      resilienceFactor: 1,
      faultToleranceType: FaultToleranceType.AZ_LEVEL
    });
    const availabilityZones = {
      'us-west-2': [
        { uuid: 'az-uuid-1', name: 'a', nodeCount: 1, preffered: 0 },
        { uuid: 'az-uuid-2', name: 'b', nodeCount: 1, preffered: 0 },
        { uuid: 'az-uuid-3', name: 'c', nodeCount: 1, preffered: 0 }
      ]
    };
    const regionList = getPlacementRegions(resilience, availabilityZones);
    expectValidPerAzReplication(regionList, availabilityZones, 3);
  });
});

describe('getPlacementRegions per-AZ RF distribution', () => {
  it('distributes RF 7 across 2 AZs with nodes 3+4 (user bug report)', () => {
    const resilience = baseResilience({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      resilienceFactor: 3
    });
    const availabilityZones = {
      'us-west-2': [
        { uuid: 'az-uuid-1', name: 'az-a', nodeCount: 3, preffered: 0 },
        { uuid: 'az-uuid-2', name: 'az-b', nodeCount: 4, preffered: 0 }
      ]
    };
    const regionList = getPlacementRegions(resilience, availabilityZones, { replicationFactor: 7 });
    const replicationFactors = expectValidPerAzReplication(regionList, availabilityZones, 7);
    expect(replicationFactors).toEqual([3, 4]);
  });

  it('distributes RF 7 across 2 AZs with nodes 4+3 (swapped capacities)', () => {
    const resilience = baseResilience({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      resilienceFactor: 3
    });
    const availabilityZones = {
      'us-west-2': [
        { uuid: 'az-uuid-1', name: 'az-a', nodeCount: 4, preffered: 0 },
        { uuid: 'az-uuid-2', name: 'az-b', nodeCount: 3, preffered: 0 }
      ]
    };
    const regionList = getPlacementRegions(resilience, availabilityZones, { replicationFactor: 7 });
    const replicationFactors = expectValidPerAzReplication(regionList, availabilityZones, 7);
    expect(replicationFactors).toEqual([4, 3]);
  });

  it('distributes RF 7 across 3 AZs with nodes 3+2+2 (expert fixture)', () => {
    const resilience = baseResilience({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      resilienceFactor: 3
    });
    const availabilityZones = {
      'us-west-2': [
        { uuid: 'az-uuid-1', name: 'az-a', nodeCount: 3, preffered: 0 },
        { uuid: 'az-uuid-2', name: 'az-b', nodeCount: 2, preffered: 0 },
        { uuid: 'az-uuid-3', name: 'az-c', nodeCount: 2, preffered: 0 }
      ]
    };
    const regionList = getPlacementRegions(resilience, availabilityZones, { replicationFactor: 7 });
    const replicationFactors = expectValidPerAzReplication(regionList, availabilityZones, 7);
    expect(replicationFactors).toEqual([3, 2, 2]);
  });

  it('keeps guided even split at 1+1+1 for RF 3', () => {
    const resilience = baseResilience({
      resilienceFactor: 1,
      faultToleranceType: FaultToleranceType.AZ_LEVEL
    });
    const availabilityZones = {
      'us-west-2': [
        { uuid: 'az-uuid-1', name: 'a', nodeCount: 1, preffered: 0 },
        { uuid: 'az-uuid-2', name: 'b', nodeCount: 1, preffered: 0 },
        { uuid: 'az-uuid-3', name: 'c', nodeCount: 1, preffered: 0 }
      ]
    };
    const regionList = getPlacementRegions(resilience, availabilityZones);
    const replicationFactors = expectValidPerAzReplication(regionList, availabilityZones, 3);
    expect(replicationFactors).toEqual([1, 1, 1]);
  });

  it('distributes RF 3 across uneven nodes 1+2 without exceeding capacity', () => {
    const resilience = baseResilience({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      resilienceFactor: 1
    });
    const availabilityZones = {
      'us-west-2': [
        { uuid: 'az-uuid-1', name: 'az-a', nodeCount: 1, preffered: 0 },
        { uuid: 'az-uuid-2', name: 'az-b', nodeCount: 2, preffered: 0 }
      ]
    };
    const regionList = getPlacementRegions(resilience, availabilityZones, { replicationFactor: 3 });
    const replicationFactors = expectValidPerAzReplication(regionList, availabilityZones, 3);
    expect(replicationFactors).toEqual([1, 2]);
  });
});

describe('distributeReplicationFactorAcrossAzs', () => {
  it('places one replica per region before filling remaining slots', () => {
    const replicationFactors = distributeReplicationFactorAcrossAzs(
      [
        { nodeCount: 2, regionUuid: 'region-1' },
        { nodeCount: 2, regionUuid: 'region-2' }
      ],
      3
    );
    expect(replicationFactors).toEqual([2, 1]);
  });
});
