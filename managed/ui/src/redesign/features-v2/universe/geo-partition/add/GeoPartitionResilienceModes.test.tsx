/**
 * Geo partition: ResilienceAndRegions with isGeoPartition + hideHelpText.
 * Guided vs expert toggle; resilience type field hidden; one validation path per mode.
 */
import { createRef } from 'react';
import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { vi } from 'vitest';
import { ResilienceAndRegions } from '../../create-universe/steps/resilence-regions/ResilienceAndRegions';
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
  RESILIENCE_FORM_MODE
} from '../../create-universe/fields/FieldNames';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { changeLanguage: () => new Promise(() => {}) }
  }),
  Trans: ({ children }: { children?: React.ReactNode }) => children ?? null
}));

vi.mock('@app/redesign/assets/map.svg', () => ({ default: () => null }));
vi.mock('@app/redesign/assets/map_selected.svg', () => ({ default: () => null }));
vi.mock('@app/redesign/assets/flash_transparent.svg', () => ({ default: () => null }));

vi.mock('../../create-universe/steps/resilence-regions/ResilienceRequirementCard', () => ({
  ResilienceRequirementCard: () => <div data-testid="guided-resilience-card" />
}));

const mockMoveToNextPage = vi.fn();
const mockSaveResilienceAndRegionsSettings = vi.fn();
const mockSaveNodesAvailabilitySettings = vi.fn();
const mockMoveToPreviousPage = vi.fn();
const mockSetResilienceType = vi.fn();

type RegionLike = {
  code?: string;
  name?: string;
  latitude?: number;
  longitude?: number;
  zones?: { code?: string }[];
};

function makeRegion(code: string, zoneCount = 0): RegionLike {
  const zones = Array.from({ length: zoneCount }, (_, i) => ({ code: `${code}-z${i}` }));
  return {
    code,
    name: `Region ${code}`,
    latitude: 0,
    longitude: 0,
    zones
  };
}

function getContextValue(overrides?: {
  faultToleranceType?: FaultToleranceType;
  resilienceFactor?: number;
  regions?: RegionLike[];
  resilienceFormMode?: ResilienceFormMode;
}) {
  const state = {
    ...initialCreateUniverseFormState,
    generalSettings: { cloud: 'aws' } as any,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [FAULT_TOLERANCE_TYPE]: overrides?.faultToleranceType ?? FaultToleranceType.REGION_LEVEL,
      [RESILIENCE_FACTOR]: overrides?.resilienceFactor ?? 1,
      [RESILIENCE_FORM_MODE]: overrides?.resilienceFormMode ?? ResilienceFormMode.GUIDED,
      [REGIONS_FIELD]: overrides?.regions ?? []
    }
  };
  const methods = createUniverseFormMethods(state as createUniverseFormProps);
  return [
    state,
    {
      ...methods,
      moveToNextPage: () => mockMoveToNextPage(),
      saveResilienceAndRegionsSettings: (data: unknown) =>
        mockSaveResilienceAndRegionsSettings(data),
      saveNodesAvailabilitySettings: (data: unknown) =>
        mockSaveNodesAvailabilitySettings(data),
      moveToPreviousPage: () => mockMoveToPreviousPage(),
      setResilienceType: (t: ResilienceType) => mockSetResilienceType(t)
    }
  ] as any;
}

const queryClient = new QueryClient({
  defaultOptions: { queries: { retry: false } }
});

function renderGeoResilience(contextValue: ReturnType<typeof getContextValue>, triggerNext = false) {
  const ref = createRef<StepsRef>();
  render(
    <QueryClientProvider client={queryClient}>
      <CreateUniverseContext.Provider value={contextValue}>
        <ResilienceAndRegions ref={ref} isGeoPartition hideHelpText />
      </CreateUniverseContext.Provider>
    </QueryClientProvider>
  );
  expect(ref.current).toBeTruthy();
  if (triggerNext) {
    ref.current!.onNext();
  }
  return { ref };
}

describe('ResilienceAndRegions (geo partition)', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('does not render resilience type quick-setup option (ResilienceTypeField hidden)', () => {
    renderGeoResilience(getContextValue());
    expect(screen.queryByText('quickSetup')).not.toBeInTheDocument();
  });

  it('shows guided mode toggle and guided resilience card by default', () => {
    renderGeoResilience(getContextValue());
    expect(screen.getByText('formType.guidedMode')).toBeInTheDocument();
    expect(screen.getByText('formType.expertMode')).toBeInTheDocument();
    expect(screen.getByTestId('guided-resilience-card')).toBeInTheDocument();
  });

  it('hides guided resilience card after switching to expert mode', () => {
    renderGeoResilience(getContextValue());
    fireEvent.click(screen.getByText('formType.expertMode'));
    expect(screen.queryByTestId('guided-resilience-card')).not.toBeInTheDocument();
  });

  it('guided: shows region validation error on Next when Region Level and no regions', async () => {
    renderGeoResilience(getContextValue(), true);
    await waitFor(() => {
      expect(screen.getByText('errMsg.regionErrTooFew')).toBeInTheDocument();
    });
  });

  it('expert: when initial form mode is Expert, guided resilience card is not shown', () => {
    renderGeoResilience(
      getContextValue({
        faultToleranceType: FaultToleranceType.NODE_LEVEL,
        resilienceFactor: 3,
        regions: [makeRegion('r0', 3)],
        resilienceFormMode: ResilienceFormMode.EXPERT_MODE
      })
    );
    expect(screen.queryByTestId('guided-resilience-card')).not.toBeInTheDocument();
    expect(screen.getByText('formType.expertMode')).toBeInTheDocument();
  });
});
