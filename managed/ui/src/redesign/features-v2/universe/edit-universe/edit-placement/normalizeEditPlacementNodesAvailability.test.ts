import { describe, expect, it } from 'vitest';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../../create-universe/steps/resilence-regions/dtos';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import { REPLICATION_FACTOR } from '../../create-universe/fields/FieldNames';
import {
  isSingleAzMode,
  minExpertRfForRegionCount,
  needsEditPlacementNodesNormalization,
  normalizeEditPlacementNodesAvailability,
  requiredAzCountForGuided
} from './normalizeEditPlacementNodesAvailability';
import { resolveEditPlacementNodesOnSave } from './EditPlacementUtils';

function makeRegion(code: string, zoneCount: number) {
  return {
    uuid: `region-${code}`,
    code,
    name: `Region ${code}`,
    zones: Array.from({ length: zoneCount }, (_, index) => ({
      uuid: `${code}-z${index}`,
      code: `${code}-z${index}`,
      name: `Z${index}`,
      subnet: `subnet-${index}`
    }))
  };
}

function guidedBase(
  overrides: Partial<ResilienceAndRegionsProps> = {}
): ResilienceAndRegionsProps {
  return {
    resilienceType: ResilienceType.REGULAR,
    resilienceFormMode: ResilienceFormMode.GUIDED,
    faultToleranceType: FaultToleranceType.AZ_LEVEL,
    resilienceFactor: 1,
    nodeCount: 3,
    regions: [makeRegion('r0', 5)],
    ...overrides
  } as ResilienceAndRegionsProps;
}

function makeFourAzPlacement(): NodeAvailabilityProps['availabilityZones'] {
  return {
    r0: Array.from({ length: 4 }, (_, index) => ({
      uuid: `r0-z${index}`,
      name: `Z${index}`,
      nodeCount: 1,
      preffered: index + 1
    }))
  };
}

const emptyZones = (): NodeAvailabilityProps => ({
  availabilityZones: {},
  useDedicatedNodes: false
});

const azNames = (n: NodeAvailabilityProps) =>
  Object.values(n.availabilityZones ?? {})
    .flat()
    .map((z) => z.name);

const hasNamedAzs = (n: NodeAvailabilityProps) => azNames(n).every(Boolean);

const countAzRows = (n: NodeAvailabilityProps) =>
  Object.values(n.availabilityZones ?? {}).reduce((acc, zones) => acc + zones.length, 0);

/** Mirrors edit placement: ResilienceAndRegions clear → resolve → normalize. */
function editNodesAfterClear(
  resilience: ResilienceAndRegionsProps,
  universeDefaults: NodeAvailabilityProps
) {
  const resolved = resolveEditPlacementNodesOnSave(emptyZones(), universeDefaults);
  return normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability: resolved });
}

describe('minExpertRfForRegionCount', () => {
  it('matches the expert RF options that stay enabled for a given region count', () => {
    expect(minExpertRfForRegionCount(1)).toBe(1);
    expect(minExpertRfForRegionCount(2)).toBe(3);
    expect(minExpertRfForRegionCount(3)).toBe(3);
    expect(minExpertRfForRegionCount(4)).toBe(5);
    expect(minExpertRfForRegionCount(5)).toBe(5);
    expect(minExpertRfForRegionCount(6)).toBe(7);
    expect(minExpertRfForRegionCount(7)).toBe(7);
    expect(minExpertRfForRegionCount(8)).toBeUndefined();
  });
});

describe('requiredAzCountForGuided', () => {
  it('returns 3 AZ rows for AZ_LEVEL with resilience factor 1', () => {
    expect(requiredAzCountForGuided(guidedBase())).toBe(3);
  });

  it('returns 1 AZ row for NODE_LEVEL and NONE', () => {
    expect(
      requiredAzCountForGuided(
        guidedBase({ faultToleranceType: FaultToleranceType.NODE_LEVEL })
      )
    ).toBe(1);
    expect(
      requiredAzCountForGuided(guidedBase({ faultToleranceType: FaultToleranceType.NONE }))
    ).toBe(1);
  });
});

describe('isSingleAzMode', () => {
  it('is true only for NODE_LEVEL and NONE', () => {
    expect(isSingleAzMode(guidedBase({ faultToleranceType: FaultToleranceType.NODE_LEVEL }))).toBe(
      true
    );
    expect(isSingleAzMode(guidedBase({ faultToleranceType: FaultToleranceType.NONE }))).toBe(true);
    expect(isSingleAzMode(guidedBase({ faultToleranceType: FaultToleranceType.AZ_LEVEL }))).toBe(
      false
    );
  });
});

describe('normalizeEditPlacementNodesAvailability', () => {
  it('trims 4 existing AZ rows to 3 when AZ_LEVEL resilience factor is 1', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: makeFourAzPlacement(),
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result?.availabilityZones.r0).toHaveLength(3);
    expect(result?.availabilityZones.r0.map((zone) => zone.name)).toEqual(['Z0', 'Z1', 'Z2']);
    expect(result?.availabilityZones.r0.map((zone) => zone.preffered)).toEqual([1, 2, 3]);
    expect(result?.[REPLICATION_FACTOR]).toBe(3);
  });

  it('collapses AZ_LEVEL placement to one region and one AZ for NODE_LEVEL', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.NODE_LEVEL,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 4)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: makeFourAzPlacement(),
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {})).toEqual(['r0']);
    expect(result?.availabilityZones.r0).toHaveLength(1);
    expect(result?.availabilityZones.r0[0].name).toBe('Z0');
    expect(result?.availabilityZones.r0[0].nodeCount).toBe(3);
  });

  it('collapses REGION_LEVEL placement to one AZ for NODE_LEVEL', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.NODE_LEVEL,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 3), makeRegion('r1', 3)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [{ uuid: 'r0-z0', name: 'Z0', nodeCount: 2, preffered: 1 }],
        r1: [{ uuid: 'r1-z0', name: 'Z0', nodeCount: 2, preffered: 1 }]
      },
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {})).toEqual(['r0']);
    expect(result?.availabilityZones.r0).toHaveLength(1);
  });

  it('expands NODE_LEVEL placement to required AZ count when switching to AZ_LEVEL', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [{ uuid: 'r0-z0', name: 'Z0', nodeCount: 3, preffered: 0 }]
      },
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result?.availabilityZones.r0).toHaveLength(3);
    expect(result?.availabilityZones.r0.map((zone) => zone.name)).toEqual(['Z0', 'Z1', 'Z2']);
    // Existing AZ keeps its preferred; newly filled AZs default to not preferred.
    expect(result?.availabilityZones.r0.map((zone) => zone.preffered)).toEqual([0, 0, 0]);
  });

  it('preserves existing preferred ranks and defaults newly filled AZs to not preferred', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [{ uuid: 'r0-z0', name: 'Z0', nodeCount: 2, preffered: 1 }]
      },
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result?.availabilityZones.r0).toHaveLength(3);
    expect(result?.availabilityZones.r0[0].preffered).toBe(1);
    expect(result?.availabilityZones.r0.slice(1).map((zone) => zone.preffered)).toEqual([0, 0]);
  });

  it('preserves existing non-first AZ by identity when expanding regions (no duplicate names)', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 2,
      regions: [makeRegion('r0', 5), makeRegion('r1', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [{ uuid: 'r0-z1', name: 'Z1', nodeCount: 5, preffered: 0 }]
      },
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });
    const r0Names = result?.availabilityZones.r0.map((zone) => zone.name) ?? [];
    const allNodeCounts = Object.values(result?.availabilityZones ?? {})
      .flat()
      .map((zone) => zone.nodeCount);

    // Existing universe AZ is listed first; remaining slots filled without duplicates.
    expect(r0Names[0]).toBe('Z1');
    expect(new Set(r0Names).size).toBe(r0Names.length);
    expect(r0Names.filter((name) => name === 'Z1')).toHaveLength(1);
    // Guided mode: all AZs follow the first (existing) AZ's node count.
    expect(allNodeCounts.every((count) => count === 5)).toBe(true);
    expect(Object.keys(result?.availabilityZones ?? {}).sort()).toEqual(['r0', 'r1']);
  });

  it('expands from first-AZ existing count and syncs that count across all guided AZs', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [{ uuid: 'r0-z0', name: 'Z0', nodeCount: 3, preffered: 0 }]
      },
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result?.availabilityZones.r0.map((zone) => zone.name)).toEqual(['Z0', 'Z1', 'Z2']);
    expect(result?.availabilityZones.r0.map((zone) => zone.nodeCount)).toEqual([3, 3, 3]);
  });

  it('removes stale region keys when selected regions change', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [
          { uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 },
          { uuid: 'r0-z1', name: 'Z1', nodeCount: 1, preffered: 2 },
          { uuid: 'r0-z2', name: 'Z2', nodeCount: 1, preffered: 3 }
        ],
        stale: [{ uuid: 'stale-z0', name: 'Stale', nodeCount: 1, preffered: 1 }]
      },
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {})).toEqual(['r0']);
    expect(result?.availabilityZones.r0).toHaveLength(3);
  });

  it('returns nodes unchanged for expert mode when region codes match', () => {
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.AZ_LEVEL
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: makeFourAzPlacement(),
      useDedicatedNodes: false,
      replicationFactor: 4
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result).toBe(nodesAndAvailability);
  });

  it('drops stale region keys and generates placement for newly selected regions', () => {
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.NONE,
      resilienceFactor: 1,
      regions: [makeRegion('r1', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [
          { uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 },
          { uuid: 'r0-z1', name: 'Z1', nodeCount: 1, preffered: 2 },
          { uuid: 'r0-z2', name: 'Z2', nodeCount: 1, preffered: 3 }
        ]
      },
      useDedicatedNodes: true,
      replicationFactor: 3
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result).not.toBe(nodesAndAvailability);
    expect(Object.keys(result?.availabilityZones ?? {})).toEqual(['r1']);
    expect(result?.availabilityZones.r1.length).toBeGreaterThan(0);
    expect(result?.useDedicatedNodes).toBe(true);
    expect(result?.[REPLICATION_FACTOR]).toBeDefined();
  });

  it('keeps original universe placement for regions that stay selected', () => {
    const existingR0 = [
      { uuid: 'r0-z0', name: 'Existing AZ', nodeCount: 2, preffered: 1 }
    ];
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.NONE,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 5), makeRegion('r1', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: { r0: existingR0 },
      useDedicatedNodes: false,
      replicationFactor: 1
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result?.availabilityZones.r0).toEqual(existingR0);
    expect(Object.keys(result?.availabilityZones ?? {}).sort()).toEqual(['r0', 'r1']);
    expect(result?.availabilityZones.r1.length).toBeGreaterThan(0);
    expect(result?.availabilityZones.r1[0].name).not.toBe('Existing AZ');
    // RF 1 is disabled for 2 regions; bump to the smallest allowed option.
    expect(result?.[REPLICATION_FACTOR]).toBe(3);
  });

  it('bumps RF when adding a 4th region would leave RF 3 disabled', () => {
    const regions = [
      makeRegion('r0', 3),
      makeRegion('r1', 3),
      makeRegion('r2', 3),
      makeRegion('r3', 3)
    ];
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.NONE,
      resilienceFactor: 3,
      regions
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [{ uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 }],
        r1: [{ uuid: 'r1-z0', name: 'Z0', nodeCount: 1, preffered: 2 }],
        r2: [{ uuid: 'r2-z0', name: 'Z0', nodeCount: 1, preffered: 3 }]
      },
      useDedicatedNodes: false,
      replicationFactor: 3
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {}).sort()).toEqual(['r0', 'r1', 'r2', 'r3']);
    expect(result?.[REPLICATION_FACTOR]).toBe(5);
  });

  it('bumps RF to 7 when selecting 7 regions would leave RF 5 disabled', () => {
    const regions = Array.from({ length: 7 }, (_, i) => makeRegion(`r${i}`, 3));
    const existingZones = Object.fromEntries(
      regions.slice(0, 5).map((region, index) => [
        region.code,
        [{ uuid: `${region.code}-z0`, name: 'Z0', nodeCount: 1, preffered: index + 1 }]
      ])
    );
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.NONE,
      resilienceFactor: 5,
      regions
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: existingZones,
      useDedicatedNodes: false,
      replicationFactor: 5
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {}).sort()).toEqual(
      regions.map((r) => r.code).sort()
    );
    expect(result?.[REPLICATION_FACTOR]).toBe(7);
  });

  it('preserves RF above the region-count floor (does not drop 7 to expert default 5)', () => {
    const regions = [
      makeRegion('r0', 3),
      makeRegion('r1', 3),
      makeRegion('r2', 3),
      makeRegion('r3', 3)
    ];
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.NONE,
      resilienceFactor: 7,
      regions
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [{ uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 }],
        r1: [{ uuid: 'r1-z0', name: 'Z0', nodeCount: 1, preffered: 2 }],
        r2: [{ uuid: 'r2-z0', name: 'Z0', nodeCount: 1, preffered: 3 }]
      },
      useDedicatedNodes: false,
      replicationFactor: 7
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result?.[REPLICATION_FACTOR]).toBe(7);
  });

  it('preserves universe RF when adding a 3rd region (expert default would be 3)', () => {
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 5,
      regions: [makeRegion('r0', 3), makeRegion('r1', 3), makeRegion('r2', 3)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [
          { uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 },
          { uuid: 'r0-z1', name: 'Z1', nodeCount: 1, preffered: 2 },
          { uuid: 'r0-z2', name: 'Z2', nodeCount: 1, preffered: 3 }
        ],
        r1: [
          { uuid: 'r1-z0', name: 'Z0', nodeCount: 1, preffered: 4 },
          { uuid: 'r1-z1', name: 'Z1', nodeCount: 1, preffered: 5 }
        ]
      },
      useDedicatedNodes: false,
      replicationFactor: 5
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {}).sort()).toEqual(['r0', 'r1', 'r2']);
    expect(result?.[REPLICATION_FACTOR]).toBe(5);
  });

  it('preserves universe RF when removing a region', () => {
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.NONE,
      resilienceFactor: 5,
      regions: [makeRegion('r0', 3), makeRegion('r1', 3)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [{ uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 }],
        r1: [{ uuid: 'r1-z0', name: 'Z0', nodeCount: 1, preffered: 2 }],
        r2: [{ uuid: 'r2-z0', name: 'Z0', nodeCount: 1, preffered: 3 }]
      },
      useDedicatedNodes: false,
      replicationFactor: 5
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {}).sort()).toEqual(['r0', 'r1']);
    expect(result?.[REPLICATION_FACTOR]).toBe(5);
  });

  it('expert NODE_LEVEL empty zones expands to all selected regions (no guided single-region collapse)', () => {
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.NODE_LEVEL,
      resilienceFactor: 5,
      regions: [makeRegion('r0', 3), makeRegion('r1', 3), makeRegion('r2', 3)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {},
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {}).sort()).toEqual(['r0', 'r1', 'r2']);
  });

  it('expert NODE_LEVEL keeps existing regions and fills only the newly selected region', () => {
    const existingR0 = [{ uuid: 'r0-z0', name: 'Existing AZ', nodeCount: 2, preffered: 1 }];
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.NODE_LEVEL,
      resilienceFactor: 5,
      regions: [makeRegion('r0', 3), makeRegion('r1', 3)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: { r0: existingR0 },
      useDedicatedNodes: false,
      replicationFactor: 5
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result?.availabilityZones.r0).toEqual(existingR0);
    expect(result?.availabilityZones.r1?.length).toBeGreaterThan(0);
    expect(Object.keys(result?.availabilityZones ?? {}).sort()).toEqual(['r0', 'r1']);
  });

  it('generates expert placement when zones are empty', () => {
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {},
      useDedicatedNodes: false,
      replicationFactor: 1
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(Object.keys(result?.availabilityZones ?? {})).toEqual(['r0']);
    expect(result?.availabilityZones.r0.length).toBeGreaterThan(0);
    expect(result?.[REPLICATION_FACTOR]).toBe(1);
  });

  it('uses expert default RF when generating placement with no seeded RF', () => {
    const resilience = guidedBase({
      resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1,
      regions: [makeRegion('r0', 5)]
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {},
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result?.[REPLICATION_FACTOR]).toBe(3);
  });

  it('returns nodes unchanged when already compatible', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1
    });
    const nodesAndAvailability: NodeAvailabilityProps = {
      availabilityZones: {
        r0: [
          { uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 },
          { uuid: 'r0-z1', name: 'Z1', nodeCount: 1, preffered: 2 },
          { uuid: 'r0-z2', name: 'Z2', nodeCount: 1, preffered: 3 }
        ]
      },
      useDedicatedNodes: false
    };

    const result = normalizeEditPlacementNodesAvailability({ resilience, nodesAndAvailability });

    expect(result).toBe(nodesAndAvailability);
  });
});

describe('needsEditPlacementNodesNormalization', () => {
  it('detects stale region keys and AZ count mismatches', () => {
    const resilience = guidedBase({
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 1
    });

    expect(
      needsEditPlacementNodesNormalization(resilience, {
        r0: [
          { uuid: 'a', name: 'Z0', nodeCount: 1, preffered: 1 },
          { uuid: 'b', name: 'Z1', nodeCount: 1, preffered: 2 },
          { uuid: 'c', name: 'Z2', nodeCount: 1, preffered: 3 }
        ]
      })
    ).toBe(false);

    expect(needsEditPlacementNodesNormalization(resilience, makeFourAzPlacement())).toBe(true);
    expect(
      needsEditPlacementNodesNormalization(resilience, {
        r0: [{ uuid: 'a', name: 'Z0', nodeCount: 1, preffered: 1 }],
        stale: [{ uuid: 'b', name: 'Stale', nodeCount: 1, preffered: 1 }]
      })
    ).toBe(true);
  });
});

describe('edit placement after ResilienceAndRegions clear', () => {
  const universeDefaults: NodeAvailabilityProps = {
    availabilityZones: {
      r0: [
        { uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 },
        { uuid: 'r0-z1', name: 'Z1', nodeCount: 1, preffered: 2 },
        { uuid: 'r0-z2', name: 'Z2', nodeCount: 1, preffered: 3 }
      ],
      r1: [{ uuid: 'r1-z0', name: 'R1Z0', nodeCount: 1, preffered: 4 }]
    },
    useDedicatedNodes: true,
    replicationFactor: 3
  };

  it('E5: guided → expert keeps universe named AZs and dedicated flag', () => {
    const result = editNodesAfterClear(
      guidedBase({
        resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
        faultToleranceType: FaultToleranceType.AZ_LEVEL,
        resilienceFactor: 3,
        regions: [makeRegion('r0', 5), makeRegion('r1', 5)]
      }),
      universeDefaults
    )!;
    expect(hasNamedAzs(result)).toBe(true);
    expect(result.useDedicatedNodes).toBe(true);
    expect(result.availabilityZones.r0.map((z) => z.uuid)).toEqual(['r0-z0', 'r0-z1', 'r0-z2']);
    expect(result.availabilityZones.r1[0].uuid).toBe('r1-z0');
  });

  it('E6: expert → guided keeps named AZs (not empty-name placeholders)', () => {
    const result = editNodesAfterClear(
      guidedBase({
        faultToleranceType: FaultToleranceType.AZ_LEVEL,
        resilienceFactor: 1,
        regions: [makeRegion('r0', 5)]
      }),
      {
        availabilityZones: { r0: universeDefaults.availabilityZones.r0 },
        useDedicatedNodes: true,
        replicationFactor: 3
      }
    )!;
    expect(hasNamedAzs(result)).toBe(true);
    expect(countAzRows(result)).toBe(requiredAzCountForGuided(guidedBase()));
  });

  it('E7: region remove keeps stayed universe AZ', () => {
    const result = editNodesAfterClear(
      guidedBase({
        resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
        faultToleranceType: FaultToleranceType.NONE,
        resilienceFactor: 1,
        regions: [makeRegion('r0', 5)]
      }),
      universeDefaults
    )!;
    expect(Object.keys(result.availabilityZones)).toEqual(['r0']);
    expect(result.availabilityZones.r0[0].uuid).toBe('r0-z0');
    expect(hasNamedAzs(result)).toBe(true);
  });

  it('E8: region add keeps stayed universe AZ and fills new region', () => {
    const result = editNodesAfterClear(
      guidedBase({
        resilienceFormMode: ResilienceFormMode.EXPERT_MODE,
        faultToleranceType: FaultToleranceType.NONE,
        resilienceFactor: 1,
        regions: [makeRegion('r0', 5), makeRegion('r1', 5), makeRegion('r2', 5)]
      }),
      {
        availabilityZones: { r0: universeDefaults.availabilityZones.r0 },
        useDedicatedNodes: true,
        replicationFactor: 1
      }
    )!;
    expect(result.availabilityZones.r0[0].uuid).toBe('r0-z0');
    expect(result.availabilityZones.r2?.length).toBeGreaterThan(0);
  });

  it('E9: guided trim keeps existing uuids where possible', () => {
    const result = editNodesAfterClear(
      guidedBase({ faultToleranceType: FaultToleranceType.AZ_LEVEL, resilienceFactor: 1 }),
      {
        availabilityZones: makeFourAzPlacement(),
        useDedicatedNodes: true
      }
    )!;
    expect(countAzRows(result)).toBe(3);
    expect(result.availabilityZones.r0.every((z) => z.uuid.startsWith('r0-z'))).toBe(true);
  });

  it('E10: guided expand keeps existing uuid/name in overlay', () => {
    const result = editNodesAfterClear(
      guidedBase({ faultToleranceType: FaultToleranceType.AZ_LEVEL, resilienceFactor: 2 }),
      {
        availabilityZones: {
          r0: [{ uuid: 'r0-z0', name: 'Z0', nodeCount: 1, preffered: 1 }]
        },
        useDedicatedNodes: true
      }
    )!;
    expect(countAzRows(result)).toBe(5);
    expect(result.availabilityZones.r0.some((z) => z.uuid === 'r0-z0' && z.name === 'Z0')).toBe(
      true
    );
  });

  it('E11: NODE_LEVEL / NONE collapses to single AZ', () => {
    for (const faultToleranceType of [
      FaultToleranceType.NODE_LEVEL,
      FaultToleranceType.NONE
    ]) {
      const resilience = guidedBase({ faultToleranceType, resilienceFactor: 1 });
      const result = editNodesAfterClear(resilience, universeDefaults)!;
      expect(countAzRows(result)).toBe(1);
      expect(hasNamedAzs(result)).toBe(true);
    }
  });

  it('E12: SINGLE_NODE yields one AZ layout', () => {
    const result = editNodesAfterClear(
      guidedBase({
        resilienceType: ResilienceType.SINGLE_NODE,
        faultToleranceType: FaultToleranceType.NONE,
        resilienceFactor: 1
      }),
      universeDefaults
    )!;
    expect(countAzRows(result)).toBe(1);
  });
});
