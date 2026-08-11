import { describe, expect, it } from 'vitest';
import {
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
  it('expert mode uses resilienceFactor as literal RF', () => {
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
    const sum = regionList.flatMap((r) => r.az_list ?? []).reduce((s, az) => s + az.replication_factor, 0);
    expect(sum).toBe(3);
  });
});
