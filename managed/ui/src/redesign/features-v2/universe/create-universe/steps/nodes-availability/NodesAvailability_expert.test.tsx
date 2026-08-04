/**
 * Expert mode: nodes step uses NodesAvailabilityExpertBody (no guided ResilienceRequirementCard).
 */
import { createRef } from 'react';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { vi } from 'vitest';
import {
  CreateUniverseContext,
  initialCreateUniverseFormState,
  createUniverseFormMethods,
  createUniverseFormProps
} from '../../CreateUniverseContext';
import { FaultToleranceType, ResilienceFormMode, ResilienceType } from '../resilence-regions/dtos';
import {
  REGIONS_FIELD,
  REPLICATION_FACTOR,
  RESILIENCE_FACTOR,
  FAULT_TOLERANCE_TYPE,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../../fields/FieldNames';
import { getNodeCount } from '../../CreateUniverseUtils';
import { NodeAvailabilityProps } from './dtos';
import { NodesAvailability } from './NodesAvailability';

vi.mock('../resilence-regions/ResilienceRequirementCard', () => ({
  ResilienceRequirementCard: () => <div data-testid="guided-resilience-requirement-card" />
}));

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { changeLanguage: () => new Promise(() => {}) }
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
const mockSaveResilienceAndRegionsSettings = vi.fn();
const mockMoveToPreviousPage = vi.fn();

type RegionLike = {
  code?: string;
  name?: string;
  uuid?: string;
  latitude?: number;
  longitude?: number;
  zones?: { code?: string; name?: string; uuid?: string }[];
};

function makeRegion(code: string, zoneCount: number): RegionLike {
  const zones = Array.from({ length: zoneCount }, (_, i) => ({
    code: `${code}-z${i}`,
    name: `Z${i}`,
    uuid: `u-${code}-${i}`
  }));
  return {
    code,
    name: `Region ${code}`,
    latitude: 0,
    longitude: 0,
    uuid: `uuid-${code}`,
    zones
  };
}

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

function makeAvailabilityZonesWithNodeCounts(
  regionCode: string,
  nodeCounts: number[]
): NodeAvailabilityProps['availabilityZones'] {
  return {
    [regionCode]: nodeCounts.map((nodeCount, i) => ({
      name: `Z${i}`,
      uuid: `u-${i}`,
      nodeCount,
      preffered: i
    }))
  };
}

function getExpertContext() {
  const r0 = makeRegion('r0', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeValidAvailabilityZonesForRegion('r0', 3)
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

/** Expert + empty zones: Figma defaults run on mount (single region, >2 AZs → RF 3). */
function getExpertContextWithFaultTolerance(
  faultToleranceType: FaultToleranceType,
  options?: { zoneCountInForm?: number; zonesInRegion?: number }
) {
  const zonesInRegion = options?.zonesInRegion ?? 3;
  const zoneCountInForm = options?.zoneCountInForm ?? 3;
  const r0 = makeRegion('r0', zonesInRegion);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: faultToleranceType,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeValidAvailabilityZonesForRegion('r0', zoneCountInForm)
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextEmptyZones() {
  const r0 = makeRegion('r0', 4);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: {}
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function mergeAvailabilityZones(
  ...parts: NodeAvailabilityProps['availabilityZones'][]
): NodeAvailabilityProps['availabilityZones'] {
  return Object.assign({}, ...parts);
}


/** Two regions selected in expert but zones only seeded for the first (stale guided node-level shape). */
function getExpertContextTwoRegionsStaleZones() {
  const r0 = makeRegion('r0', 3);
  const r1 = makeRegion('r1', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0, r1] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeValidAvailabilityZonesForRegion('r0', 1)
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}


function getExpertContextAmbiguousInferredResilience() {
  const r0 = makeRegion('r0', 2);
  const r1 = makeRegion('r1', 2);
  const r2 = makeRegion('r2', 2);
  const r3 = makeRegion('r3', 2);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0, r1, r2, r3] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: mergeAvailabilityZones(
        makeValidAvailabilityZonesForRegion('r0', 1),
        makeValidAvailabilityZonesForRegion('r1', 1),
        makeValidAvailabilityZonesForRegion('r2', 1),
        makeValidAvailabilityZonesForRegion('r3', 1)
      )
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextAzGreaterThanRf() {
  const r0 = makeRegion('r0', 4);
  const r1 = makeRegion('r1', 4);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0, r1] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: mergeAvailabilityZones(
        makeValidAvailabilityZonesForRegion('r0', 2),
        makeValidAvailabilityZonesForRegion('r1', 2)
      )
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextNoSelectedAzs() {
  const r0 = makeRegion('r0', 0);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: {}
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextWithBlankAz() {
  const r0 = makeRegion('r0', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: {
        r0: [{ name: '', uuid: '', nodeCount: 3, preffered: 0 }]
      }
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextRfFiveThreeAzThreeNodes() {
  const r0 = makeRegion('r0', 3);
  const r1 = makeRegion('r1', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
      [RESILIENCE_FACTOR]: 5,
      [REGIONS_FIELD]: [r0, r1] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: mergeAvailabilityZones(
        makeValidAvailabilityZonesForRegion('r0', 2),
        makeValidAvailabilityZonesForRegion('r1', 1)
      )
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextRfFiveThreeAzFourNodes() {
  const r0 = makeRegion('r0', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
      [RESILIENCE_FACTOR]: 5,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeAvailabilityZonesWithNodeCounts('r0', [2, 1, 1])
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextRfFiveThreeAzFiveNodes(
  faultToleranceType: FaultToleranceType = FaultToleranceType.NODE_LEVEL
) {
  const r0 = makeRegion('r0', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: faultToleranceType,
      [RESILIENCE_FACTOR]: 5,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeAvailabilityZonesWithNodeCounts('r0', [3, 1, 1])
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextRfFiveFiveAzSingleRegion() {
  const r0 = makeRegion('r0', 5);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 5,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeValidAvailabilityZonesForRegion('r0', 5)
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

/** Expert: 3 AZs, 7 nodes total (RF 7) — RF decrease should trim nodes when AZ count already ≤ RF. */
function getExpertContextRfSevenThreeAzSevenNodes() {
  const r0 = makeRegion('r0', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 7,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      [REPLICATION_FACTOR]: 7,
      availabilityZones: makeAvailabilityZonesWithNodeCounts('r0', [3, 2, 2])
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

/** Expert: 5 AZs × 2 nodes (RF 5) — RF decrease trims AZs first, then nodes to match RF. */
function getExpertContextRfFiveFiveAzTwoNodesEach() {
  const r0 = makeRegion('r0', 5);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 5,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      [REPLICATION_FACTOR]: 5,
      availabilityZones: {
        r0: [0, 1, 2, 3, 4].map((i) => ({
          name: `Z${i}`,
          uuid: `u-${i}`,
          nodeCount: 2,
          preffered: i
        }))
      }
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextRfFiveFiveAzTwoRegions() {
  const r0 = makeRegion('r0', 3);
  const r1 = makeRegion('r1', 2);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 5,
      [REGIONS_FIELD]: [r0, r1] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: {
        r0: [
          { name: 'Z0', uuid: 'u-r0-0', nodeCount: 1, preffered: 0 },
          { name: 'Z1', uuid: 'u-r0-1', nodeCount: 1, preffered: 1 },
          { name: 'Z2', uuid: 'u-r0-2', nodeCount: 1, preffered: 2 }
        ],
        r1: [
          { name: 'Z0', uuid: 'u-r1-0', nodeCount: 1, preffered: 0 },
          { name: 'Z1', uuid: 'u-r1-1', nodeCount: 1, preffered: 1 }
        ]
      }
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextAtRfCap() {
  const r0 = makeRegion('r0', 4);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeValidAvailabilityZonesForRegion('r0', 3)
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextRegionExhaustedOnly() {
  const r0 = makeRegion('r0', 2);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeValidAvailabilityZonesForRegion('r0', 2)
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getExpertContextAtRfCapAndRegionExhausted() {
  const r0 = makeRegion('r0', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 3,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeValidAvailabilityZonesForRegion('r0', 3)
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

function getGuidedContext() {
  const r0 = makeRegion('r0', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1,
      [REGIONS_FIELD]: [r0] as any
    },
    nodesAvailabilitySettings: {
      ...initialCreateUniverseFormState.nodesAvailabilitySettings!,
      availabilityZones: makeValidAvailabilityZonesForRegion('r0', 3)
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
      saveResilienceAndRegionsSettings: mockSaveResilienceAndRegionsSettings,
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

const queryClient = new QueryClient({
  defaultOptions: { queries: { retry: false } }
});

function renderNodes(contextValue: ReturnType<typeof getExpertContext>) {
  const ref = createRef<any>();
  render(
    <QueryClientProvider client={queryClient}>
      <CreateUniverseContext.Provider value={contextValue}>
        <NodesAvailability ref={ref} />
      </CreateUniverseContext.Provider>
    </QueryClientProvider>
  );
  expect(ref.current).toBeTruthy();
  return ref;
}

describe('NodesAvailability expert mode', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('does not render guided ResilienceRequirementCard', () => {
    renderNodes(getExpertContext());
    expect(screen.queryByTestId('guided-resilience-requirement-card')).not.toBeInTheDocument();
  });

  it('renders map and availability UI', () => {
    renderNodes(getExpertContext());
    expect(screen.getByTestId('yb-maps-nodes-availability')).toBeInTheDocument();
    expect(screen.getByTestId('add-availability-zone-button')).toBeInTheDocument();
  });

  it('resyncs placement so both regions get cards when zones keys lag selected regions (guided→expert)', async () => {
    renderNodes(getExpertContextTwoRegionsStaleZones());
    await waitFor(() => {
      expect(screen.getByText(/Region r0/)).toBeInTheDocument();
      expect(screen.getByText(/Region r1/)).toBeInTheDocument();
    });
  });

  it('renders expert replication factor control', () => {
    renderNodes(getExpertContext());
    expect(screen.getByText('replicationFactorLabel')).toBeInTheDocument();
  });

  it('shows not resilient inferred resilience card for RF=1', () => {
    renderNodes(getExpertContext());
    expect(screen.getByTestId('inferred-resilience-card')).toBeInTheDocument();
    expect(screen.getByText('notResilientBody')).toBeInTheDocument();
  });

  it('renders resilient inferred resilience card for resilient topology', () => {
    renderNodes(getExpertContextWithFaultTolerance(FaultToleranceType.AZ_LEVEL));
    expect(screen.getByTestId('inferred-resilience-card')).toBeInTheDocument();
    expect(screen.getByText('resilientMessage')).toBeInTheDocument();
  });

  it('hides inferred resilience card for ambiguous/in-between topology', () => {
    renderNodes(getExpertContextAmbiguousInferredResilience());
    expect(screen.queryByTestId('inferred-resilience-card')).not.toBeInTheDocument();
  });

  it('hides inferred resilience card when selected AZs exceed RF', () => {
    renderNodes(getExpertContextAzGreaterThanRf());
    expect(screen.queryByTestId('inferred-resilience-card')).not.toBeInTheDocument();
  });

  it('hides inferred resilience card when there are no selected AZs', () => {
    renderNodes(getExpertContextNoSelectedAzs());
    expect(screen.queryByTestId('inferred-resilience-card')).not.toBeInTheDocument();
  });

  it('hides inferred resilience card for under-provisioned NODE_LEVEL topology', () => {
    renderNodes(getExpertContextRfFiveThreeAzThreeNodes());
    expect(screen.queryByTestId('inferred-resilience-card')).not.toBeInTheDocument();
  });

  it('allows Next when selected resilience differs from inferred in expert mode', async () => {
    const ref = renderNodes(getExpertContextWithFaultTolerance(FaultToleranceType.REGION_LEVEL));
    await ref.current.onNext();
    await waitFor(() => {
      expect(mockMoveToNextPage).toHaveBeenCalled();
      expect(screen.queryByText('errMsg.expertResilienceMismatch')).not.toBeInTheDocument();
    });
  });

  it('blocks Next when current RF/AZ placement is not inferable', async () => {
    const ref = renderNodes(getExpertContextAzGreaterThanRf());
    await ref.current.onNext();
    await waitFor(() => {
      expect(mockMoveToNextPage).not.toHaveBeenCalled();
      expect(screen.getByText('errMsg.expertResilienceUninferable')).toBeInTheDocument();
    });
  });

  it('blocks Next when no AZ is selected', async () => {
    const ref = renderNodes(getExpertContextNoSelectedAzs());
    await ref.current.onNext();
    await waitFor(() => {
      expect(mockMoveToNextPage).not.toHaveBeenCalled();
      expect(screen.getByText('errMsg.expertResilienceUninferable')).toBeInTheDocument();
    });
  });

  it('blocks Next when total nodes are below RF in expert mode', async () => {
    const ref = renderNodes(getExpertContextRfFiveThreeAzFourNodes());
    await ref.current.onNext();
    await waitFor(() => {
      expect(mockMoveToNextPage).not.toHaveBeenCalled();
      expect(screen.getByText('errMsg.expertResilienceUninferable')).toBeInTheDocument();
    });
  });

  it('blocks Next when any selected AZ is blank', async () => {
    const ref = renderNodes(getExpertContextWithBlankAz());
    await ref.current.onNext();
    await waitFor(() => {
      expect(mockMoveToNextPage).not.toHaveBeenCalled();
      expect(screen.getByText('errMsg.expertAzBlank')).toBeInTheDocument();
    });
  });

  it('allows Next for RF=5 with 3 AZs and node distribution 3+1+1 when selected FT is NODE_LEVEL', async () => {
    const ref = renderNodes(getExpertContextRfFiveThreeAzFiveNodes(FaultToleranceType.NODE_LEVEL));
    await ref.current.onNext();
    await waitFor(() => {
      expect(mockMoveToNextPage).toHaveBeenCalled();
      expect(screen.queryByText('errMsg.expertResilienceMismatch')).not.toBeInTheDocument();
      expect(screen.queryByText('errMsg.expertResilienceUninferable')).not.toBeInTheDocument();
    });
  });

  it('allows Next for RF=5 with 3 AZs and node distribution 3+1+1 when selected FT mismatches inferred', async () => {
    const ref = renderNodes(getExpertContextRfFiveThreeAzFiveNodes(FaultToleranceType.AZ_LEVEL));
    await ref.current.onNext();
    await waitFor(() => {
      expect(mockMoveToNextPage).toHaveBeenCalled();
      expect(screen.queryByText('errMsg.expertResilienceMismatch')).not.toBeInTheDocument();
    });
  });

  it('applies expert default RF and syncs context when zones are empty on mount', async () => {
    renderNodes(getExpertContextEmptyZones());
    await waitFor(() => {
      expect(mockSaveResilienceAndRegionsSettings).toHaveBeenCalledWith(
        expect.objectContaining({
          [RESILIENCE_FACTOR]: 3
        })
      );
    });
  });

  it('shows Add Availability Zone for expert mode with region-level fault tolerance', () => {
    renderNodes(getExpertContextWithFaultTolerance(FaultToleranceType.REGION_LEVEL));
    expect(screen.getByTestId('add-availability-zone-button')).toBeInTheDocument();
  });

  it('keeps per-AZ node count inputs editable in expert mode for region-level fault tolerance', () => {
    renderNodes(getExpertContextWithFaultTolerance(FaultToleranceType.REGION_LEVEL));
    const nodeInputs = screen.getAllByTestId('availability-zone-node-count-input');
    expect(nodeInputs.length).toBeGreaterThan(0);
    nodeInputs.forEach((wrapper) => {
      expect(within(wrapper).getByRole('spinbutton')).not.toBeDisabled();
    });
  });

  it('keeps Add Availability Zone enabled in expert mode (free-form), even with node-level state', () => {
    renderNodes(
      getExpertContextWithFaultTolerance(FaultToleranceType.NODE_LEVEL, {
        zoneCountInForm: 2,
        zonesInRegion: 3
      })
    );
    const addBtn = screen.getByTestId('add-availability-zone-button');
    expect(addBtn).toBeInTheDocument();
    expect(addBtn).not.toBeDisabled();
  });

  it('disables Add Availability Zone at expert RF cap and shows RF tooltip', async () => {
    renderNodes(getExpertContextAtRfCap());
    const addBtn = screen.getByTestId('add-availability-zone-button');
    expect(addBtn).toBeDisabled();
    fireEvent.mouseOver(addBtn);
    await waitFor(() => {
      expect(screen.getByText('tooltips.addAvailabilityZoneDisabledExpert')).toBeInTheDocument();
    });
  });

  it('shows region AZ exhaustion tooltip when only region cap applies', async () => {
    renderNodes(getExpertContextRegionExhaustedOnly());
    const addBtn = screen.getByTestId('add-availability-zone-button');
    expect(addBtn).toBeDisabled();
    fireEvent.mouseOver(addBtn);
    await waitFor(() => {
      expect(screen.getByText('tooltips.regionNoMoreAz')).toBeInTheDocument();
    });
  });

  it('prefers RF-cap tooltip over region-cap tooltip when both apply', async () => {
    renderNodes(getExpertContextAtRfCapAndRegionExhausted());
    const addBtn = screen.getByTestId('add-availability-zone-button');
    expect(addBtn).toBeDisabled();
    fireEvent.mouseOver(addBtn);
    await waitFor(() => {
      expect(screen.getByText('tooltips.addAvailabilityZoneDisabledExpert')).toBeInTheDocument();
      expect(screen.queryByText('tooltips.regionNoMoreAz')).not.toBeInTheDocument();
    });
  });

  it('auto-trims overflow AZs when RF decreases (single region: 5 AZ -> RF 3)', async () => {
    const ref = renderNodes(getExpertContextRfFiveFiveAzSingleRegion());
    expect(screen.getAllByTestId('availability-zone-select')).toHaveLength(5);

    ref.current.setValue(REPLICATION_FACTOR, 3);

    await waitFor(() => {
      expect(screen.getAllByTestId('availability-zone-select')).toHaveLength(3);
    });

    await ref.current.onNext();
    await waitFor(() => {
      expect(mockSaveNodesAvailabilitySettings).toHaveBeenCalled();
      const saved = mockSaveNodesAvailabilitySettings.mock.calls.at(-1)?.[0] as NodeAvailabilityProps;
      expect(saved.availabilityZones.r0).toHaveLength(3);
      expect(saved.availabilityZones.r0.map((z) => z.preffered)).toEqual([0, 1, 2]);
    });
  });

  it('auto-trims one AZ per region per pass and removes lowest-priority preferred AZs first', async () => {
    const ref = renderNodes(getExpertContextRfFiveFiveAzTwoRegions());
    expect(screen.getAllByTestId('availability-zone-select')).toHaveLength(5);

    ref.current.setValue(REPLICATION_FACTOR, 3);

    await waitFor(() => {
      expect(screen.getAllByTestId('availability-zone-select')).toHaveLength(3);
    });

    await ref.current.onNext();
    await waitFor(() => {
      expect(mockSaveNodesAvailabilitySettings).toHaveBeenCalled();
      const saved = mockSaveNodesAvailabilitySettings.mock.calls.at(-1)?.[0] as NodeAvailabilityProps;
      expect(saved.availabilityZones.r0.map((z) => z.name)).toEqual(['Z0', 'Z1']);
      expect(saved.availabilityZones.r1.map((z) => z.name)).toEqual(['Z0']);
      expect(saved.availabilityZones.r0.map((z) => z.preffered)).toEqual([0, 1]);
      expect(saved.availabilityZones.r1.map((z) => z.preffered)).toEqual([0]);
    });
  });

  it('auto-reduces total node count when RF drops and AZ count already ≤ RF', async () => {
    const ref = renderNodes(getExpertContextRfSevenThreeAzSevenNodes());
    expect(screen.getAllByTestId('availability-zone-select')).toHaveLength(3);

    ref.current.setValue(REPLICATION_FACTOR, 5);

    await waitFor(() => {
      expect(screen.getAllByTestId('availability-zone-select')).toHaveLength(3);
    });

    await ref.current.onNext();
    await waitFor(() => {
      expect(mockSaveNodesAvailabilitySettings).toHaveBeenCalled();
      const saved = mockSaveNodesAvailabilitySettings.mock.calls.at(-1)?.[0] as NodeAvailabilityProps;
      expect(saved.availabilityZones.r0).toHaveLength(3);
      expect(getNodeCount(saved.availabilityZones)).toBe(5);
      expect(saved.availabilityZones.r0.map((z) => z.nodeCount)).toEqual([3, 1, 1]);
    });
  });

  it('trims AZs before reducing nodes when both exceed RF on RF decrease', async () => {
    const ref = renderNodes(getExpertContextRfFiveFiveAzTwoNodesEach());
    expect(screen.getAllByTestId('availability-zone-select')).toHaveLength(5);

    ref.current.setValue(REPLICATION_FACTOR, 3);

    await waitFor(() => {
      expect(screen.getAllByTestId('availability-zone-select')).toHaveLength(3);
    });

    await ref.current.onNext();
    await waitFor(() => {
      expect(mockSaveNodesAvailabilitySettings).toHaveBeenCalled();
      const saved = mockSaveNodesAvailabilitySettings.mock.calls.at(-1)?.[0] as NodeAvailabilityProps;
      expect(saved.availabilityZones.r0).toHaveLength(3);
      expect(getNodeCount(saved.availabilityZones)).toBe(3);
      expect(saved.availabilityZones.r0.map((z) => z.nodeCount)).toEqual([1, 1, 1]);
      expect(saved.availabilityZones.r0.map((z) => z.name)).toEqual(['Z0', 'Z1', 'Z2']);
    });
  });

  it('clears expert-mode validation errors when RF changes', async () => {
    const ref = renderNodes(getExpertContextAzGreaterThanRf());

    await ref.current.onNext();
    await waitFor(() => {
      expect(screen.getByText('errMsg.expertResilienceUninferable')).toBeInTheDocument();
    });

    ref.current.setValue(REPLICATION_FACTOR, 5);

    await waitFor(() => {
      expect(screen.queryByText('errMsg.expertResilienceUninferable')).not.toBeInTheDocument();
    });
  });

});

describe('NodesAvailability guided mode (mock card)', () => {
  it('renders mocked ResilienceRequirementCard', () => {
    renderNodes(getGuidedContext());
    expect(screen.getByTestId('guided-resilience-requirement-card')).toBeInTheDocument();
  });
});
