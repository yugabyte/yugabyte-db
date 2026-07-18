/*
 * Unit tests: Resilience and Regions step.
 * Covers validation for Region/AZ/Node fault tolerance, multiple resilience factors,
 * error display, and valid (no-error) paths.
 * Paths align with Figma: error states (create-universe guided mode), defaults, and
 * UI validation flow (region/AZ/node counts, fault tolerance types).
 * Scenarios align with test_cases.json TC001–TC026 (resilience and region pages only).
 *
 * Validation and error-clearing: Tests align with validation on Next
 * (setShowErrorsAfterSubmit(true) + handleSubmit), ValidationSchema (region count,
 * AZ count, node-level single region, NONE), and error clearing when form becomes
 * valid (useEffect in ResilienceAndRegions.tsx); valid-state tests cover "no error when valid".
 */

import { createRef } from 'react';
import { render, screen, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ResilienceAndRegions } from './ResilienceAndRegions';
import {
  CreateUniverseContext,
  initialCreateUniverseFormState,
  createUniverseFormMethods,
  createUniverseFormProps
} from '../../CreateUniverseContext';
import { FaultToleranceType, ResilienceType } from './dtos';
import {
  REGIONS_FIELD,
  RESILIENCE_FACTOR,
  FAULT_TOLERANCE_TYPE
} from '../../fields/FieldNames';

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

const mockMoveToNextPage = vi.fn();
const mockSaveResilienceAndRegionsSettings = vi.fn();
const mockSaveNodesAvailabilitySettings = vi.fn();
const mockMoveToPreviousPage = vi.fn();
const mockSetResilienceType = vi.fn();

// --- Shared test helpers (modular, reusable) ---

type ZoneLike = { code?: string };
type RegionLike = {
  code?: string;
  name?: string;
  latitude?: number;
  longitude?: number;
  zones?: ZoneLike[];
};

/** Build a single region with optional zone count (for AZ_LEVEL). */
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

/** Build N regions; optionally each with zonesPerRegion zones (for AZ_LEVEL). */
function makeRegions(
  count: number,
  zonesPerRegion: number = 0
): RegionLike[] {
  return Array.from({ length: count }, (_, i) =>
    makeRegion(`r${i}`, zonesPerRegion)
  );
}

/** Regions with total AZ count = totalZones (one region with that many zones). */
function makeRegionsWithTotalZones(totalZones: number): RegionLike[] {
  if (totalZones <= 0) return [];
  return [makeRegion('r0', totalZones)];
}

function getContextValue(overrides?: {
  faultToleranceType?: FaultToleranceType;
  resilienceFactor?: number;
  regions?: RegionLike[];
}) {
  const state = {
    ...initialCreateUniverseFormState,
    generalSettings: { cloud: 'aws' } as any,
    resilienceAndRegionsSettings: {
      ...initialCreateUniverseFormState.resilienceAndRegionsSettings!,
      [FAULT_TOLERANCE_TYPE]: overrides?.faultToleranceType ?? FaultToleranceType.REGION_LEVEL,
      [RESILIENCE_FACTOR]: overrides?.resilienceFactor ?? 1,
      [REGIONS_FIELD]: overrides?.regions ?? []
    }
  };
  const methods = createUniverseFormMethods(state as createUniverseFormProps);
  return [
    state,
    {
      ...methods,
      moveToNextPage: () => mockMoveToNextPage(),
      saveResilienceAndRegionsSettings: (data: any) =>
        mockSaveResilienceAndRegionsSettings(data),
      saveNodesAvailabilitySettings: (data: any) =>
        mockSaveNodesAvailabilitySettings(data),
      moveToPreviousPage: () => mockMoveToPreviousPage(),
      setResilienceType: (t: ResilienceType) => mockSetResilienceType(t)
    }
  ] as any;
}

const queryClient = new QueryClient({
  defaultOptions: { queries: { retry: false } }
});

/** Render Resilience step and trigger Next; use for all tests. */
function renderResilienceAndTriggerNext(contextValue: ReturnType<typeof getContextValue>) {
  const ref = createRef<any>();
  render(
    <QueryClientProvider client={queryClient}>
      <CreateUniverseContext.Provider value={contextValue}>
        <ResilienceAndRegions ref={ref} />
      </CreateUniverseContext.Provider>
    </QueryClientProvider>
  );
  expect(ref.current).toBeTruthy();
  ref.current.onNext();
  return { ref };
}

/** Render Resilience step without triggering Next; for same-session error-clearing tests. */
function renderResilience(contextValue: ReturnType<typeof getContextValue>) {
  const ref = createRef<any>();
  render(
    <QueryClientProvider client={queryClient}>
      <CreateUniverseContext.Provider value={contextValue}>
        <ResilienceAndRegions ref={ref} />
      </CreateUniverseContext.Provider>
    </QueryClientProvider>
  );
  expect(ref.current).toBeTruthy();
  return { ref };
}

// --- Tests ---

describe('ResilienceAndRegions', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  describe('Region Level', () => {
    it('shows region count error when Guided + Region Level and 0 regions and Next is triggered', async () => {
      renderResilienceAndTriggerNext(getContextValue());
      await waitFor(() => {
        expect(screen.getByText('errMsg.regionErrTooFew')).toBeInTheDocument();
      });
    });

    it('shows field-level regions error when Guided + Region Level and 0 regions and Next is triggered', async () => {
      renderResilienceAndTriggerNext(getContextValue());
      await waitFor(() => {
        expect(screen.getByText('errMsg.regions')).toBeInTheDocument();
      });
    });

    it('shows region error for Region Level RF 2 with 0 regions', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 2,
          regions: []
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.regionErrTooFew')).toBeInTheDocument();
      });
    });

    it('shows no region error for Region Level RF 2 with 5 regions', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 2,
          regions: makeRegions(5)
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.regionErrExceeds')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.regionErrTooFew')).not.toBeInTheDocument();
      });
    });

    it('shows region count error for Region Level RF 2 with more than 5 regions (TC011)', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 2,
          regions: makeRegions(6)
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.regionErrExceeds')).toBeInTheDocument();
      });
    });

    it('shows region error for Region Level RF 3 with 0 regions', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 3,
          regions: []
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.regionErrTooFew')).toBeInTheDocument();
      });
    });

    it('shows no region error for Region Level RF 3 with 7 regions', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 3,
          regions: makeRegions(7)
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.regionErrExceeds')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.regionErrTooFew')).not.toBeInTheDocument();
      });
    });

    it('shows field-level regionFieldRegionsFew when Region Level RF 2 with 1 region (need 5)', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 2,
          regions: makeRegions(1)
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.regionFieldRegionsFew')).toBeInTheDocument();
      });
    });
  });

  describe('Availability (AZ_LEVEL)', () => {
    it('shows AZ error for AZ_LEVEL RF 1 with 0 regions', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: []
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.azErrFew')).toBeInTheDocument();
      });
    });

    it('shows AZ error for AZ_LEVEL RF 1 with 2 AZs total', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 1,
          regions: makeRegionsWithTotalZones(2)
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.azErrFew')).toBeInTheDocument();
      });
    });

    it('shows no fault-tolerance error for AZ_LEVEL RF 2 with 5 AZs', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 2,
          regions: makeRegionsWithTotalZones(5)
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.azErrFew')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.azErrMany')).not.toBeInTheDocument();
      });
    });

    it('shows AZ many error when AZ_LEVEL RF 2 with more regions than required and too few AZs', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 2,
          regions: makeRegions(6, 0)
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.azErrMany')).toBeInTheDocument();
      });
    });

    it('shows field-level regionFieldAzFew when AZ_LEVEL RF 2 with 1 zone total (need 5)', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.AZ_LEVEL,
          resilienceFactor: 2,
          regions: makeRegionsWithTotalZones(1)
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.regionFieldAzFew')).toBeInTheDocument();
      });
    });
  });

  describe('Nodes (NODE_LEVEL)', () => {
    it('shows node error for NODE_LEVEL with 2 regions', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          regions: makeRegions(2)
        })
      );
      await waitFor(() => {
        expect(screen.getByText('errMsg.nodeErr')).toBeInTheDocument();
      });
    });

    it('shows no node error for NODE_LEVEL with 1 region', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NODE_LEVEL,
          regions: makeRegions(1)
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.nodeErr')).not.toBeInTheDocument();
      });
    });
  });

  describe('NONE', () => {
    it('shows no fault-tolerance alert for NONE with 0 regions', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.NONE,
          regions: []
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.regionErrExceeds')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.regionErrTooFew')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.azErrFew')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.nodeErr')).not.toBeInTheDocument();
      });
    });
  });

  describe('Error validation and clearing', () => {
    it('valid Region Level RF 1 with 3 regions does not show region error after Next', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 1,
          regions: makeRegions(3)
        })
      );
      await waitFor(() => {
        expect(screen.queryByText('errMsg.regionErrExceeds')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.regionErrTooFew')).not.toBeInTheDocument();
      });
    });

    it('valid state calls saveResilienceAndRegionsSettings on Next', async () => {
      renderResilienceAndTriggerNext(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 1,
          regions: makeRegions(3)
        })
      );
      await waitFor(() => {
        expect(mockSaveResilienceAndRegionsSettings).toHaveBeenCalled();
      });
    });

    it('same-session error clearing: error appears on Next with invalid config, then disappears after setting valid regions', async () => {
      const { ref } = renderResilience(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 1,
          regions: []
        })
      );
      ref.current.onNext();
      await waitFor(() => {
        expect(screen.getByText('errMsg.regionErrTooFew')).toBeInTheDocument();
      });
      ref.current.setValue!(REGIONS_FIELD, makeRegions(3));
      await waitFor(() => {
        expect(screen.queryByText('errMsg.regionErrExceeds')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.regionErrTooFew')).not.toBeInTheDocument();
      });
    });

    it('changing fault tolerance goal hides submit errors until Next again', async () => {
      const { ref } = renderResilience(
        getContextValue({
          faultToleranceType: FaultToleranceType.REGION_LEVEL,
          resilienceFactor: 1,
          regions: makeRegions(4)
        })
      );
      ref.current.onNext();
      await waitFor(() => {
        expect(screen.getByText('errMsg.regionErrExceeds')).toBeInTheDocument();
        expect(screen.getByText('errMsg.regionFieldRegionsFew')).toBeInTheDocument();
      });
      ref.current.setValue!(FAULT_TOLERANCE_TYPE, FaultToleranceType.AZ_LEVEL);
      await waitFor(() => {
        expect(screen.queryByText('errMsg.regionErrExceeds')).not.toBeInTheDocument();
        expect(screen.queryByText('errMsg.regionFieldRegionsFew')).not.toBeInTheDocument();
      });
      ref.current.onNext();
      await waitFor(() => {
        // 4 regions with RF1 AZ-level: too many regions surfaces before AZ count (schema order).
        expect(screen.getByText('errMsg.azErrMany')).toBeInTheDocument();
      });
    });
  });
});
