import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { describe, expect, it, vi, beforeEach } from 'vitest';
import { mainTheme } from '@app/redesign/theme/mainTheme';
import { YBToastProvider } from '../../create-universe/helpers/ToastUtils';
import { EditUniverseContext, EditUniverseTabs } from '../EditUniverseContext';
import { PlacementTab } from './PlacementTab';
import type { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  makeGeoUniverse,
  makeNonGeoUniverse,
  makeNonGeoUniverseWithReadReplicaPartitions,
  makeNonGeoUniverseWithReadReplicaPlacementSpec,
  makeProviderRegions,
  makeSingleGeoPartitionUniverse
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
  EditPlacement: ({ visible, onSubmit }: any) =>
    visible ? (
      <button
        data-testid="edit-placement-submit-stub"
        onClick={() =>
          onSubmit({
            resilience: {
              resilienceType: 'Regular',
              resilienceFormMode: 'Expert Mode',
              faultToleranceType: 'AZ_LEVEL',
              resilienceFactor: 1,
              nodeCount: 1,
              regions: [
                {
                  uuid: 'region-uuid-1',
                  code: 'us-west-2',
                  name: 'US West',
                  zones: [
                    {
                      uuid: 'az-uuid-1',
                      code: 'us-west-2a',
                      name: 'az-a',
                      subnet: 's1'
                    }
                  ]
                }
              ]
            },
            nodesAndAvailability: {
              availabilityZones: {
                'us-west-2': [
                  {
                    uuid: 'az-uuid-1',
                    name: 'az-a',
                    nodeCount: 2,
                    preffered: 1
                  }
                ]
              },
              useDedicatedNodes: false
            }
          })
        }
      >
        submit
      </button>
    ) : null
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
          <YBToastProvider>
            <EditUniverseContext.Provider
              value={{
                activeTab: EditUniverseTabs.PLACEMENT,
                universeData,
                providerRegions: makeProviderRegions()
              }}
            >
              <PlacementTab />
            </EditUniverseContext.Provider>
          </YBToastProvider>
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

  it('renders single primary geo partition as a regular primary placement card', () => {
    renderPlacementTab(makeSingleGeoPartitionUniverse());
    expect(screen.queryByTestId('addGeoPartition')).not.toBeInTheDocument();
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(1);
  });

  it('submits single primary geo partition placement edits through partitions_spec', async () => {
    const user = userEvent.setup();
    renderPlacementTab(makeSingleGeoPartitionUniverse());

    await user.click(screen.getByTestId('edit-placement-edit-button'));
    const menu = await screen.findByRole('menu');
    await user.click(menu.querySelector('[data-test-id="edit-placement-auto-balance"]')!);
    await user.click(await screen.findByTestId('edit-placement-submit-stub'));

    expect(hoisted.mutateEditUniverse).toHaveBeenCalledWith(
      expect.objectContaining({
        data: expect.objectContaining({
          clusters: [
            expect.objectContaining({
              uuid: 'primary-cluster-uuid',
              partitions_spec: expect.any(Array)
            })
          ]
        })
      }),
      expect.any(Object)
    );
    const mutationArg = hoisted.mutateEditUniverse.mock.calls[0][0];
    expect(mutationArg.data.clusters[0].placement_spec).toBeUndefined();
    expect(mutationArg.data.clusters[0].partitions_spec[0].uuid).toBe('geo-part-1');
  });

  it('non-geo with read replica (placement_spec only): primary and one RR card', () => {
    renderPlacementTab(makeNonGeoUniverseWithReadReplicaPlacementSpec());
    const editButtons = screen.getAllByTestId('edit-placement-edit-button');
    expect(editButtons).toHaveLength(2);
  });

  it('async cluster with partitions_spec does not trigger geo placement view when primary has no partitions', () => {
    renderPlacementTab(makeNonGeoUniverseWithReadReplicaPartitions());
    expect(screen.queryByTestId('addGeoPartition')).not.toBeInTheDocument();
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(3);
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
