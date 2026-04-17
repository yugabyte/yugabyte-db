/**
 * Geo partition: NodesAvailability with isGeoPartition — guided vs expert layouts
 * (geo uses row layout + NodeInstanceDetails when AddGeoPartitionContext has universeData).
 */
import { createRef } from 'react';
import { render, screen } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { vi } from 'vitest';
import { NodesAvailability } from '../../create-universe/steps/nodes-availability/NodesAvailability';
import {
  CreateUniverseContext,
  initialCreateUniverseFormState,
  createUniverseFormMethods,
  createUniverseFormProps,
  StepsRef
} from '../../create-universe/CreateUniverseContext';
import { FaultToleranceType, ResilienceFormMode, ResilienceType } from '../../create-universe/steps/resilence-regions/dtos';
import {
  REGIONS_FIELD,
  RESILIENCE_FACTOR,
  FAULT_TOLERANCE_TYPE,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../../create-universe/fields/FieldNames';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import {
  AddGeoPartitionContext,
  initialAddGeoPartitionFormState,
  addGeoPartitionFormMethods
} from './AddGeoPartitionContext';
import { ClusterSpecClusterType, UniverseRespResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

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

vi.mock('./NodeInstanceDetails', () => ({
  NodeInstanceDetails: () => <div data-testid="geo-node-instance-details" />
}));

vi.mock('../../create-universe/steps/resilence-regions/ResilienceRequirementCard', () => ({
  ResilienceRequirementCard: () => <div data-testid="nodes-resilience-requirement-card" />
}));

vi.mock('@app/redesign/assets/map.svg', () => ({ default: () => null }));
vi.mock('@app/redesign/assets/map_selected.svg', () => ({ default: () => null }));

const mockMoveToNextPage = vi.fn();
const mockSaveNodesAvailabilitySettings = vi.fn();
const mockMoveToPreviousPage = vi.fn();

type RegionLike = {
  code?: string;
  name?: string;
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
  return { code, name: `Region ${code}`, latitude: 0, longitude: 0, zones };
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

function mockUniverseData(): UniverseRespResponse {
  return {
    info: { universe_uuid: 'universe-1', arch: 'x86_64' } as any,
    spec: {
      clusters: [
        {
          cluster_type: ClusterSpecClusterType.PRIMARY,
          replication_factor: 3,
          provider_spec: { provider: 'provider-1' },
          placement_spec: {
            cloud_list: [{ code: 'aws', uuid: 'cloud-1', region_list: [] }]
          }
        }
      ]
    } as any
  } as UniverseRespResponse;
}

function getNodesContext(formMode: ResilienceFormMode) {
  const r0 = makeRegion('r0', 3);
  const state = {
    ...initialCreateUniverseFormState,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: formMode,
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
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }
  ] as any;
}

const queryClient = new QueryClient({
  defaultOptions: { queries: { retry: false } }
});

function renderNodesAvailability(
  createUniverseCtx: ReturnType<typeof getNodesContext>,
  options: { isGeoPartition: boolean; universeData: UniverseRespResponse | undefined }
) {
  const ref = createRef<StepsRef>();
  const geoState = {
    ...initialAddGeoPartitionFormState,
    universeData: options.universeData
  };
  const geoMethods = addGeoPartitionFormMethods(geoState);
  const geoValue = [geoState, geoMethods] as any;

  const inner = (
    <CreateUniverseContext.Provider value={createUniverseCtx}>
      <NodesAvailability ref={ref} isGeoPartition={options.isGeoPartition} />
    </CreateUniverseContext.Provider>
  );

  render(
    <QueryClientProvider client={queryClient}>
      {options.isGeoPartition ? (
        <AddGeoPartitionContext.Provider value={geoValue}>{inner}</AddGeoPartitionContext.Provider>
      ) : (
        inner
      )}
    </QueryClientProvider>
  );
  expect(ref.current).toBeTruthy();
}

describe('NodesAvailability (geo partition)', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('guided + isGeoPartition + universeData shows geo instance details and nodes requirement card', () => {
    renderNodesAvailability(getNodesContext(ResilienceFormMode.GUIDED), {
      isGeoPartition: true,
      universeData: mockUniverseData()
    });
    expect(screen.getByTestId('nodes-resilience-requirement-card')).toBeInTheDocument();
    expect(screen.getByTestId('geo-node-instance-details')).toBeInTheDocument();
  });

  it('guided + isGeoPartition without universeData does not render geo layout content', () => {
    renderNodesAvailability(getNodesContext(ResilienceFormMode.GUIDED), {
      isGeoPartition: true,
      universeData: undefined
    });
    expect(screen.queryByTestId('geo-node-instance-details')).not.toBeInTheDocument();
  });

  it('guided + create-universe (not geo) does not render geo instance details', () => {
    renderNodesAvailability(getNodesContext(ResilienceFormMode.GUIDED), {
      isGeoPartition: false,
      universeData: mockUniverseData()
    });
    expect(screen.getByTestId('nodes-resilience-requirement-card')).toBeInTheDocument();
    expect(screen.queryByTestId('geo-node-instance-details')).not.toBeInTheDocument();
  });

  it('expert + isGeoPartition + universeData shows inferred resilience card and geo instance details', () => {
    renderNodesAvailability(getNodesContext(ResilienceFormMode.EXPERT_MODE), {
      isGeoPartition: true,
      universeData: mockUniverseData()
    });
    expect(screen.getByTestId('inferred-resilience-card')).toBeInTheDocument();
    expect(screen.getByTestId('geo-node-instance-details')).toBeInTheDocument();
  });

  it('expert + create-universe (not geo) shows inferred card but not geo instance details', () => {
    renderNodesAvailability(getNodesContext(ResilienceFormMode.EXPERT_MODE), {
      isGeoPartition: false,
      universeData: mockUniverseData()
    });
    expect(screen.getByTestId('inferred-resilience-card')).toBeInTheDocument();
    expect(screen.queryByTestId('geo-node-instance-details')).not.toBeInTheDocument();
  });
});
