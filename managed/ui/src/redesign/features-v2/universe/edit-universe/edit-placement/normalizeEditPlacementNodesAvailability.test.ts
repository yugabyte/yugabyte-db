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
  needsEditPlacementNodesNormalization,
  normalizeEditPlacementNodesAvailability,
  requiredAzCountForGuided
} from './normalizeEditPlacementNodesAvailability';

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
