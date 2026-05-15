import { render, screen } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { describe, expect, it, vi, beforeEach } from 'vitest';
import { mainTheme } from '@app/redesign/theme/mainTheme';
import { EditUniverseContext, EditUniverseTabs } from '../EditUniverseContext';
import { GeoPartitionPlacementView } from './GeoPartitionPlacementView';
import type { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  makeGeoUniverse,
  makeGeoUniverseWithReadReplicaPartitions,
  makeGeoUniverseWithReadReplicaPlacementSpecOnly,
  makeProviderRegions
} from '../__fixtures__/editUniverseFixtures';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { changeLanguage: () => new Promise(() => {}) }
  }),
  Trans: ({ children }: { children?: React.ReactNode }) => children ?? null
}));

vi.mock('react-toastify', () => ({
  toast: { error: vi.fn(), success: vi.fn(), info: vi.fn() }
}));

const hoisted = vi.hoisted(() => ({
  mutateEditUniverse: vi.fn()
}));

vi.mock('@app/v2/api/universe/universe', async (importOriginal) => {
  const actual = await importOriginal<typeof import('@app/v2/api/universe/universe')>();
  return {
    ...actual,
    useEditUniverse: () => ({
      mutate: hoisted.mutateEditUniverse,
      isLoading: false
    }),
    useDeleteCluster: () => ({
      mutateAsync: vi.fn().mockResolvedValue({ task_uuid: 'task-1' }),
      isLoading: false
    })
  };
});

vi.mock('../hooks/useEditUniverseTaskHandler', () => ({
  useEditUniverseTaskHandler: () => vi.fn()
}));

vi.mock('../hooks/useApplyMasterAllocation', () => ({
  useApplyMasterAllocation: () => ({
    applyMasterAllocation: vi.fn(),
    isSubmitting: false
  })
}));

vi.mock('./EditPlacement', () => ({
  EditPlacement: () => <div data-testid="edit-placement-stub" />
}));

vi.mock('../../create-universe/helpers/RegionToFlagUtils', () => ({
  getFlagFromRegion: () => <span data-testid="flag-stub" />
}));

vi.mock('@app/redesign/assets/edit2.svg', () => ({ default: () => null }));
vi.mock('@app/redesign/assets/delete2.svg', () => ({ default: () => null }));
vi.mock('@app/redesign/assets/add.svg', () => ({ default: () => null }));

const noopStore = createStore(() => ({}));

function renderGeoView(universeData: Universe) {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } }
  });
  return render(
    <ThemeProvider theme={mainTheme}>
      <Provider store={noopStore}>
        <QueryClientProvider client={queryClient}>
          <EditUniverseContext.Provider
            value={{
              activeTab: EditUniverseTabs.PLACEMENT,
              universeData,
              providerRegions: makeProviderRegions()
            }}
          >
            <GeoPartitionPlacementView />
          </EditUniverseContext.Provider>
        </QueryClientProvider>
      </Provider>
    </ThemeProvider>
  );
}

describe('GeoPartitionPlacementView', () => {
  beforeEach(() => {
    hoisted.mutateEditUniverse.mockClear();
  });

  it('shows Add Geo Partition and one edit control per primary partition', () => {
    renderGeoView(makeGeoUniverse());
    expect(screen.getByTestId('addGeoPartition')).toBeInTheDocument();
    expect(screen.getByTestId('edit-placement-actions-button')).toBeInTheDocument();
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(2);
  });

  it('renders read replica partition cards when ASYNC has partitions_spec', () => {
    renderGeoView(makeGeoUniverseWithReadReplicaPartitions());
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(4);
  });

  it('does not render read replica placement cards when ASYNC only has placement_spec', () => {
    renderGeoView(makeGeoUniverseWithReadReplicaPlacementSpecOnly());
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(2);
    expect(screen.queryByText('readReplica')).not.toBeInTheDocument();
  });
});
