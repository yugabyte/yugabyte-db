/*
 * Unit tests: Nodes Availability step.
 * Covers AZ_LEVEL and NODE_LEVEL validation (zone count, node count) for different
 * resilience factors, error display, and valid (no-error) paths.
 * Scenarios align with test_cases.json TC001–TC026 (resilience and region pages only).
 *
 * Validation and error-clearing: Tests cover validation on Next and ValidationSchema
 * (AZ count, node count, preferred continuity). Error clearing when form becomes valid
 * is implemented in useNodesAvailabilityStep.ts (useEffect); valid-state tests cover "no error when valid".
 */

import { createRef } from 'react';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { NodesAvailability } from './NodesAvailability';
import {
  CreateUniverseContext,
  initialCreateUniverseFormState,
  createUniverseFormMethods,
  createUniverseFormProps
} from '../../CreateUniverseContext';
import { FaultToleranceType, ResilienceFormMode, ResilienceType } from '../resilence-regions/dtos';
import {
  NODE_COUNT,
  REGIONS_FIELD,
  RESILIENCE_FACTOR,
  FAULT_TOLERANCE_TYPE,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../../fields/FieldNames';
import { NodeAvailabilityProps } from './dtos';
import { NodesAvailabilitySchema } from './ValidationSchema';
import { assignRegionsAZNodeByReplicationFactor } from '../../CreateUniverseUtils';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { changeLanguage: () => new Promise(() => { }) }
  }),
  Trans: ({
    children,
    i18nKey
  }: {
    children?: React.ReactNode;
    i18nKey?: string;
  }) => children ?? i18nKey ?? null
}));

const mockMoveToNextPage = vi.fn();
const mockSaveNodesAvailabilitySettings = vi.fn();
const mockMoveToPreviousPage = vi.fn();

// --- Helpers (modular, reusable) ---

type ZoneLike = { code?: string; name?: string; uuid?: string; nodeCount?: number; preffered?: number };
type RegionLike = {
  code?: string;
  name?: string;
  latitude?: number;
  longitude?: number;
  zones?: ZoneLike[];
};

function makeRegion(code: string, zoneCount: number): RegionLike {
  const zones = Array.from({ length: zoneCount }, (_, i) => ({
    code: `${code}-z${i}`,
    name: `Z${i}`,
    uuid: `u-${i}`
  }));
  return { code, name: `Region ${code}`, latitude: 0, longitude: 0, zones };
}

/** Build availabilityZones for NODE_LEVEL invalid case: one region, total nodeCount < faultToleranceNeeded */
function makeAvailabilityZonesWithNodeCount(
  regionCode: string,
  totalNodeCount: number
): NodeAvailabilityProps['availabilityZones'] {
  return {
    [regionCode]: [
      {
        name: 'Z1',
        uuid: 'u1',
        nodeCount: totalNodeCount,
        preffered: 0
      }
    ]
  };
}

/** Three AZs, three nodes (RF1 AZ_LEVEL), preferred ranks 0,1,3 — gap at rank 2. */
function makeAvailabilityZonesPreferredGapRf1NodeLevel(
  regionCode: string
): NodeAvailabilityProps['availabilityZones'] {
  return {
    [regionCode]: [
      { name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 0 },
      { name: 'Z1', uuid: 'u-1', nodeCount: 1, preffered: 1 },
      { name: 'Z2', uuid: 'u-2', nodeCount: 1, preffered: 3 }
    ]
  };
}

/** Build valid availabilityZones for one region with contiguous preferred ranks (0, 1, ...). */
function makeValidAvailabilityZonesForRegion(
  regionCode: string,
  zoneCount: number
): NodeAvailabilityProps['availabilityZones'] {
  return {
    [regionCode]: Array.from({ length: zoneCount }, (_, i) => ({
      name: `Z${i}`,
      uuid: `u-${i}`,
      nodeCount: 1,
      preffered: i
    }))
  };
}

/** Three regions (each with one AZ named Z0, uuid u-0 per makeRegion), gap in preferred ranks. */
function makeAvailabilityZonesThreeRegionsPreferredGap(): NodeAvailabilityProps['availabilityZones'] {
  return {
    r0: [{ name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 0 }],
    r1: [{ name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 1 }],
    r2: [{ name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 3 }]
  };
}

function getContextValue(overrides?: {
  faultToleranceType?: FaultToleranceType;
  resilienceFactor?: number;
  regions?: RegionLike[];
  nodesAvailabilitySettings?: Partial<NodeAvailabilityProps>;
}) {
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
      [FAULT_TOLERANCE_TYPE]: overrides?.faultToleranceType ?? FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: overrides?.resilienceFactor ?? 1,
      [REGIONS_FIELD]: overrides?.regions ?? []
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      ...overrides?.nodesAvailabilitySettings
    }
  };
  const methods = createUniverseFormMethods(state as createUniverseFormProps);
  return [
    state,
    {
      ...methods,
      moveToNextPage: () => mockMoveToNextPage(),
      saveNodesAvailabilitySettings: (data: NodeAvailabilityProps) =>
        mockSaveNodesAvailabilitySettings(data),
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

const queryClient = new QueryClient({
  defaultOptions: { queries: { retry: false } }
});

function renderNodesAndTriggerNext(contextValue: ReturnType<typeof getContextValue>) {
  const ref = createRef<any>();
  render(
    <QueryClientProvider client={queryClient}>
      <CreateUniverseContext.Provider value={contextValue}>
        <NodesAvailability ref={ref} />
      </CreateUniverseContext.Provider>
    </QueryClientProvider>
  );
  expect(ref.current).toBeTruthy();
  ref.current.onNext();
  return { ref };
}

/** Render Nodes step without triggering Next; for same-session error-clearing tests. */
function renderNodes(contextValue: ReturnType<typeof getContextValue>) {
  const ref = createRef<any>();
  render(
    <QueryClientProvider client={queryClient}>
      <CreateUniverseContext.Provider value={contextValue}>
        <NodesAvailability ref={ref} />
      </CreateUniverseContext.Provider>
    </QueryClientProvider>
  );
  expect(ref.current).toBeTruthy();
  return { ref };
}

// --- Tests ---

const minimalResilience = {
  [RESILIENCE_TYPE]: ResilienceType.REGULAR,
  [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
  [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
  [RESILIENCE_FACTOR]: 1,
  [NODE_COUNT]: 1,
  [REGIONS_FIELD]: [
    { code: 'r0', name: 'R0', uuid: 'R0', latitude: 0, longitude: 0, zones: [] },
    { code: 'r1', name: 'R1', uuid: 'R1', latitude: 0, longitude: 0, zones: [] },
    { code: 'r2', name: 'R2', uuid: 'R2', latitude: 0, longitude: 0, zones: [] }
  ] as any
};

describe('NodesAvailabilitySchema', () => {
  it('rejects gap in preferred ranks (multi-region AZ_LEVEL)', () => {
    const schema = NodesAvailabilitySchema(minimalResilience);
    expect(() =>
      schema.validateSync({
        availabilityZones: makeAvailabilityZonesThreeRegionsPreferredGap(),

        useDedicatedNodes: false
      } as any)
    ).toThrow();
  });

  it('rejects gap in preferred ranks (single region AZ_LEVEL)', () => {
    const schema = NodesAvailabilitySchema({
      ...minimalResilience,
      [REGIONS_FIELD]: [{ code: 'r0', name: 'R0', uuid: 'R0', latitude: 0, longitude: 0, zones: [] }] as any
    });
    expect(() =>
      schema.validateSync({
        availabilityZones: makeAvailabilityZonesPreferredGapRf1NodeLevel('r0'),
        useDedicatedNodes: false
      } as any)
    ).toThrow();
  });

  it('accepts duplicate preferred ranks when in order (e.g. 1,1,2)', () => {
    const schema = NodesAvailabilitySchema({
      ...minimalResilience,
      [REGIONS_FIELD]: [{ code: 'r0', name: 'R0', uuid: 'R0', latitude: 0, longitude: 0, zones: [] }] as any
    });
    expect(() =>
      schema.validateSync({
        availabilityZones: {
          r0: [
            { name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 1 },
            { name: 'Z1', uuid: 'u-1', nodeCount: 1, preffered: 1 },
            { name: 'Z2', uuid: 'u-2', nodeCount: 1, preffered: 2 }
          ]
        },

        useDedicatedNodes: false
      } as any)
    ).not.toThrow();
  });

  it('rejects preferred ranks with a gap (e.g. 1,1,3)', () => {
    const schema = NodesAvailabilitySchema({
      ...minimalResilience,
      [REGIONS_FIELD]: [{ code: 'r0', name: 'R0', uuid: 'R0', latitude: 0, longitude: 0, zones: [] }] as any
    });
    expect(() =>
      schema.validateSync({
        availabilityZones: {
          r0: [
            { name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 1 },
            { name: 'Z1', uuid: 'u-1', nodeCount: 1, preffered: 1 },
            { name: 'Z2', uuid: 'u-2', nodeCount: 1, preffered: 3 }
          ]
        },

        useDedicatedNodes: false
      } as any)
    ).toThrow();
  });

  it('rejects NODE_LEVEL guided when more than one availability zone is selected', () => {
    const schema = NodesAvailabilitySchema({
      ...minimalResilience,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
      [REGIONS_FIELD]: [{ code: 'r0', name: 'R0', uuid: 'R0', latitude: 0, longitude: 0, zones: [] }] as any
    });
    expect(() =>
      schema.validateSync({
        availabilityZones: {
          r0: [
            { name: 'Z0', uuid: 'u-0', nodeCount: 2, preffered: 0 },
            { name: 'Z1', uuid: 'u-1', nodeCount: 2, preffered: 1 }
          ]
        },

        useDedicatedNodes: false
      } as any)
    ).toThrow();
  });

  it('rejects NODE_LEVEL guided when AZ count is three (exactly one required)', () => {
    const schema = NodesAvailabilitySchema({
      ...minimalResilience,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
      [RESILIENCE_FACTOR]: 1,
      [REGIONS_FIELD]: [{ code: 'r0', name: 'R0', uuid: 'R0', latitude: 0, longitude: 0, zones: [] }] as any
    });
    expect(() =>
      schema.validateSync({
        availabilityZones: {
          r0: [
            { name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 0 },
            { name: 'Z1', uuid: 'u-1', nodeCount: 1, preffered: 1 },
            { name: 'Z2', uuid: 'u-2', nodeCount: 1, preffered: 2 }
          ]
        },

        useDedicatedNodes: false
      } as any)
    ).toThrow();
  });

  it('rejects NODE_LEVEL when more than one region has availability zones', () => {
    const schema = NodesAvailabilitySchema({
      ...minimalResilience,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
      [REGIONS_FIELD]: [
        { code: 'r0', name: 'R0', uuid: 'R0', latitude: 0, longitude: 0, zones: [] },
        { code: 'r1', name: 'R1', uuid: 'R1', latitude: 0, longitude: 0, zones: [] }
      ] as any
    });
    expect(() =>
      schema.validateSync({
        availabilityZones: {
          r0: [{ name: 'Z0', uuid: 'u-0', nodeCount: 3, preffered: 0 }],
          r1: [{ name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 0 }]
        },

        useDedicatedNodes: false
      } as any)
    ).toThrow();
  });
});

describe('assignRegionsAZNodeByReplicationFactor', () => {
  it('AZ_LEVEL RF1 with 2 regions splits 3 AZs as 2+1 (Figma guided defaults)', () => {
    const availabilityZones = assignRegionsAZNodeByReplicationFactor({
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1,
      [NODE_COUNT]: 1,
      [REGIONS_FIELD]: [makeRegion('r0', 4), makeRegion('r1', 4)] as any
    });

    expect(availabilityZones.r0).toHaveLength(2);
    expect(availabilityZones.r1).toHaveLength(1);
    expect(
      Object.values(availabilityZones)
        .flat()
        .every((z) => z.nodeCount >= 1)
    ).toBe(true);
  });

  it('AZ_LEVEL RF2 with regions [2,3] initializes to 2+3 AZs', () => {
    const availabilityZones = assignRegionsAZNodeByReplicationFactor({
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 2,
      [NODE_COUNT]: 1,
      [REGIONS_FIELD]: [makeRegion('r0', 2), makeRegion('r1', 3)] as any
    });

    expect(availabilityZones.r0).toHaveLength(2);
    expect(availabilityZones.r1).toHaveLength(3);
    expect(
      Object.values(availabilityZones)
        .flat()
        .every((z) => z.nodeCount >= 1)
    ).toBe(true);
  });

  it('AZ_LEVEL uses all available AZs when required AZs exceed availability', () => {
    const availabilityZones = assignRegionsAZNodeByReplicationFactor({
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 3, // needs 7 AZ logical target
      [NODE_COUNT]: 1,
      [REGIONS_FIELD]: [makeRegion('r0', 2), makeRegion('r1', 2)] as any // only 4 available
    });

    const allZones = Object.values(availabilityZones).flat();
    expect(availabilityZones.r0).toHaveLength(2);
    expect(availabilityZones.r1).toHaveLength(2);
    expect(allZones).toHaveLength(4);
    expect(allZones.every((z) => z.nodeCount >= 1)).toBe(true);
  });
});

describe('NodesAvailability', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  describe('Availability (AZ_LEVEL)', () => {
    it('shows AZ error for AZ_LEVEL RF 1 when zones count is 2 (need 3)', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 2)]
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.azCountTooFew')).toBeInTheDocument();
      });
    });

    it('shows no AZ error for AZ_LEVEL RF 1 when zones count is 3', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3)]
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.azCountTooFew')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.azCountTooMany')).not.toBeInTheDocument();
      });
    });

    it('shows azZeroNodes when an AZ has node count 0', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3)],
          nodesAvailabilitySettings: {
            availabilityZones: {
              r0: [
                { name: 'Z0', uuid: 'u0', nodeCount: 0, preffered: 0 },
                { name: 'Z1', uuid: 'u1', nodeCount: 1, preffered: 1 },
                { name: 'Z2', uuid: 'u2', nodeCount: 1, preffered: 2 }
              ]
            },
            useDedicatedNodes: false
          }
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.azZeroNodes')).toBeInTheDocument();
      });
    });

    it('shows AZ error for AZ_LEVEL RF 2 when zones count is 4 (need 5)', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 2,
          regions: [makeRegion('r0', 4)]
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.azCountTooFew')).toBeInTheDocument();
      });
    });

    it('shows no AZ error for AZ_LEVEL RF 2 when zones count is 5', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 2,
          regions: [makeRegion('r0', 5)]
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.azCountTooFew')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.azCountTooMany')).not.toBeInTheDocument();
      });
    });

    it('shows no AZ error for AZ_LEVEL RF 3 when zones count is 7 (TC003)', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 3,
          regions: [makeRegion('r0', 7)]
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.azCountTooFew')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.azCountTooMany')).not.toBeInTheDocument();
      });
    });
  });

  describe('Nodes (NODE_LEVEL)', () => {
    it('does not show Add Availability Zone button for guided NODE_LEVEL', () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3)]
        })
      );
      expect(screen.queryByTestId('add-availability-zone-button')).not.toBeInTheDocument();
    });

    it('shows Add Availability Zone button for guided AZ_LEVEL', () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3)]
        })
      );
      expect(screen.getByTestId('add-availability-zone-button')).toBeInTheDocument();
    });

    it('shows lessNodes error for NODE_LEVEL RF 1 when total node count is 2 (need 3)', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 1)],
          nodesAvailabilitySettings: {
            availabilityZones: makeAvailabilityZonesWithNodeCount('r0', 2),
            useDedicatedNodes: false
          }
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.lessNodes')).toBeInTheDocument();
      });
    });

    it('shows no lessNodes error for NODE_LEVEL when total node count meets requirement', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 1)]
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.lessNodes')).not.toBeInTheDocument();
      });
    });

    it('shows no lessNodes error for NODE_LEVEL RF 2 when node count is 5 (TC014)', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 2,
          regions: [makeRegion('r0', 1)]
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.lessNodes')).not.toBeInTheDocument();
      });
    });

    it('shows no lessNodes error for NODE_LEVEL RF 3 when node count is 7 (TC015)', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 3,
          regions: [makeRegion('r0', 1)]
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.lessNodes')).not.toBeInTheDocument();
      });
    });

    it('shows lessNodesDedicated error for NODE_LEVEL RF 1 with useDedicatedNodes and 2 nodes', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 1)],
          nodesAvailabilitySettings: {
            availabilityZones: makeAvailabilityZonesWithNodeCount('r0', 2),
            useDedicatedNodes: true
          }
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.lessNodesDedicated')).toBeInTheDocument();
      });
    });

    it('shows nodeLevelGuidedExactlyOneAz when NODE_LEVEL has two availability zones', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 2)],
          nodesAvailabilitySettings: {
            availabilityZones: {
              r0: [
                { name: 'Z0', uuid: 'u-0', nodeCount: 2, preffered: 0 },
                { name: 'Z1', uuid: 'u-1', nodeCount: 1, preffered: 1 }
              ]
            },
            useDedicatedNodes: false
          }
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.nodeLevelGuidedExactlyOneAz')).toBeInTheDocument();
        expect(screen.queryByText('errMsg.nodeLevelOneRegionOneAz')).not.toBeInTheDocument();
      });
    });

    it('shows nodeLevelGuidedExactlyOneAz when NODE_LEVEL has three availability zones', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3)],
          nodesAvailabilitySettings: {
            availabilityZones: makeValidAvailabilityZonesForRegion('r0', 3),
            useDedicatedNodes: false
          }
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.nodeLevelGuidedExactlyOneAz')).toBeInTheDocument();
      });
    });

    it('shows nodeLevelOneRegionOneAz when NODE_LEVEL has zones in multiple regions', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 1), makeRegion('r1', 1)],
          nodesAvailabilitySettings: {
            availabilityZones: {
              r0: [{ name: 'Z0', uuid: 'u-0', nodeCount: 3, preffered: 0 }],
              r1: [{ name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 0 }]
            },
            useDedicatedNodes: false
          }
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.nodeLevelOneRegionOneAz')).toBeInTheDocument();
      });
    });
  });

  describe('Error validation and clearing', () => {
    it('valid AZ_LEVEL state calls saveNodesAvailabilitySettings on Next', async () => {
      renderNodesAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3)]
        })
      );
      await waitFor(() => {
        expect(mockSaveNodesAvailabilitySettings).toHaveBeenCalled();
      });
    });

    it('same-session error clearing: error appears on Next with invalid zone count, then disappears after setting valid availabilityZones', async () => {
      const { ref } = renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 2)],
          nodesAvailabilitySettings: {
            availabilityZones: makeValidAvailabilityZonesForRegion('r0', 2),
            useDedicatedNodes: false
          }
        })
      );
      ref.current.onNext();
      await waitFor(() => {
        expect(screen.getByText('errMsg.azCountTooFew')).toBeInTheDocument();
      });
      ref.current.setValue!('availabilityZones', makeValidAvailabilityZonesForRegion('r0', 3));
      await waitFor(() => {
        expect(screen.queryByText('errMsg.azCountTooFew')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.azCountTooMany')).not.toBeInTheDocument();
      });
    });
  });

  describe('Tooltips', () => {
    it('shows region tooltip and per-region add-az-disabled tooltip when region has no more AZs', async () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 1)]
        })
      );

      fireEvent.mouseOver(screen.getByText(/Region r0/i));
      await waitFor(() => {
        expect(screen.getByText('tooltips.regionTag')).toBeInTheDocument();
      });

      fireEvent.mouseOver(screen.getByText('add_button'));
      await waitFor(() => {
        expect(screen.getByText('tooltips.regionNoMoreAz')).toBeInTheDocument();
      });
    });

    it('shows addAvailabilityZoneDisabled tooltip when max total AZ count is reached (takes precedence)', async () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3), makeRegion('r1', 3)],
          nodesAvailabilitySettings: {
            availabilityZones: {
              r0: [{ name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 0 }],
              r1: [
                { name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 1 },
                { name: 'Z1', uuid: 'u-1', nodeCount: 1, preffered: 2 }
              ]
            },
            useDedicatedNodes: false
          }
        })
      );

      const addButtons = screen.getAllByText('add_button');
      fireEvent.mouseOver(addButtons[0]);
      await waitFor(() => {
        expect(screen.getByText('tooltips.addAvailabilityZoneDisabled')).toBeInTheDocument();
      });
    });

    it('shows guided node-count tooltip only for disabled node input', async () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 1), makeRegion('r1', 1), makeRegion('r2', 1)]
        })
      );

      const nodeInputs = screen.getAllByTestId('availability-zone-node-count-input');
      expect(nodeInputs).toHaveLength(3);
      expect(within(nodeInputs[0]).getByRole('spinbutton')).not.toBeDisabled();
      expect(within(nodeInputs[1]).getByRole('spinbutton')).toBeDisabled();
      expect(within(nodeInputs[2]).getByRole('spinbutton')).toBeDisabled();

      fireEvent.mouseOver(nodeInputs[1]);
      await waitFor(() => {
        expect(screen.getByText('tooltips.guidedNodeCount')).toBeInTheDocument();
      });
    });

    it('keeps node-count input editable for first region\'s first AZ in REGION_LEVEL fault tolerance', async () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 1), makeRegion('r1', 1)]
        })
      );

      const nodeInputs = screen.getAllByTestId('availability-zone-node-count-input');
      expect(nodeInputs).toHaveLength(2);
      expect(within(nodeInputs[0]).getByRole('spinbutton')).toBeEnabled();
      expect(within(nodeInputs[1]).getByRole('spinbutton')).toBeDisabled();
    });
  });

  describe('Add Availability Zone visibility (guided mode)', () => {
    it('does not show Add Availability Zone for REGION_LEVEL fault tolerance', () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3), makeRegion('r1', 3)]
        })
      );
      expect(screen.queryByTestId('add-availability-zone-button')).not.toBeInTheDocument();
    });

    it('does not show Add Availability Zone for NONE fault tolerance', () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.NONE,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3)]
        })
      );
      expect(screen.queryByTestId('add-availability-zone-button')).not.toBeInTheDocument();
    });
  });

  describe('AZ name uniqueness in selector', () => {
    it('disables AZ names that are already selected in another row', async () => {
      renderNodes(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: [makeRegion('r0', 3)],
          nodesAvailabilitySettings: {
            availabilityZones: {
              r0: [
                { name: 'Z0', uuid: 'u-0', nodeCount: 1, preffered: 0 },
                { name: 'Z1', uuid: 'u-1', nodeCount: 1, preffered: 1 }
              ]
            },
            nodeCountPerAz: 1,
            useDedicatedNodes: false
          } as any
        })
      );

      const azSelects = screen.getAllByLabelText('Availability Zone');
      fireEvent.mouseDown(azSelects[1]);

      const listbox = await screen.findByRole('listbox');
      const selectedElsewhereOption = within(listbox).getByRole('option', { name: 'Z0' });
      const currentSelectionOption = within(listbox).getByRole('option', { name: 'Z1' });

      expect(selectedElsewhereOption).toHaveAttribute('aria-disabled', 'true');
      expect(currentSelectionOption).not.toHaveAttribute('aria-disabled', 'true');
    });
  });
});
