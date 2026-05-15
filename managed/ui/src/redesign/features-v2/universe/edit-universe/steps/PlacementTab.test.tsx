import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { describe, expect, it, vi, beforeEach } from 'vitest';
import { mainTheme } from '@app/redesign/theme/mainTheme';
import { EditUniverseContext, EditUniverseTabs } from '../EditUniverseContext';
import { PlacementTab } from './PlacementTab';
import type { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  makeGeoUniverse,
  makeNonGeoUniverse,
  makeNonGeoUniverseWithReadReplicaPartitions,
  makeNonGeoUniverseWithReadReplicaPlacementSpec,
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

vi.mock('../edit-placement/EditPlacement', () => ({
  EditPlacement: () => <div data-testid="edit-placement-stub" />
}));

vi.mock('../../create-universe/helpers/RegionToFlagUtils', () => ({
  getFlagFromRegion: () => <span data-testid="flag-stub" />
}));

vi.mock('@app/redesign/assets/edit2.svg', () => ({ default: () => null }));
vi.mock('@app/redesign/assets/delete2.svg', () => ({ default: () => null }));

const noopStore = createStore(() => ({}));

function renderPlacementTab(universeData: Universe) {
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
            <PlacementTab />
          </EditUniverseContext.Provider>
        </QueryClientProvider>
      </Provider>
    </ThemeProvider>
  );
}

describe('PlacementTab', () => {
  beforeEach(() => {
    hoisted.mutateEditUniverse.mockClear();
  });

  it('renders non-geo primary placement without Add Geo Partition control', () => {
    renderPlacementTab(makeNonGeoUniverse());
    expect(screen.queryByTestId('addGeoPartition')).not.toBeInTheDocument();
    expect(screen.getAllByTestId('edit-placement-actions').length).toBeGreaterThanOrEqual(1);
  });

  it('renders geo placement view with Add Geo Partition', () => {
    renderPlacementTab(makeGeoUniverse());
    expect(screen.getByTestId('addGeoPartition')).toBeInTheDocument();
  });

  it('non-geo with read replica (placement_spec only): primary and one RR card', () => {
    renderPlacementTab(makeNonGeoUniverseWithReadReplicaPlacementSpec());
    const editButtons = screen.getAllByTestId('edit-placement-edit-button');
    expect(editButtons).toHaveLength(2);
  });

  it('async cluster with partitions_spec triggers geo placement view (getExistingGeoPartitions); shows two RR partition cards when primary has no partitions', () => {
    renderPlacementTab(makeNonGeoUniverseWithReadReplicaPartitions());
    expect(screen.getByTestId('addGeoPartition')).toBeInTheDocument();
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(2);
  });

  it('non-geo without read replica does not mount delete read replica modal content', () => {
    renderPlacementTab(makeNonGeoUniverse());
    expect(screen.queryByTestId('delete-read-replica-modal')).not.toBeInTheDocument();
  });

  it('read replica card edit menu exposes delete read replica action', async () => {
    const user = userEvent.setup();
    renderPlacementTab(makeNonGeoUniverseWithReadReplicaPlacementSpec());
    const editButtons = screen.getAllByTestId('edit-placement-edit-button');
    await user.click(editButtons[1]);
    const menu = await screen.findByRole('menu');
    expect(menu.querySelector('[data-test-id="read-replica-delete"]')).toBeTruthy();
    expect(menu.querySelector('[data-test-id="read-replica-edit-placement"]')).toBeTruthy();
  });
});
