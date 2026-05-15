import { render, screen } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { describe, expect, it, vi } from 'vitest';
import { mainTheme } from '@app/redesign/theme/mainTheme';
import { EditUniverseContext, EditUniverseTabs } from '../EditUniverseContext';
import { HardwareTab } from './HardwareTab';
import {
  NodeDetailsDedicatedTo,
  type Universe
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  makeNonGeoUniverse,
  makeNonGeoUniverseWithDedicatedNodeDetails,
  makeNonGeoUniverseWithReadReplicaPlacementSpec,
  makeProviderRegions
} from '../__fixtures__/editUniverseFixtures';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string, opts?: Record<string, string>) =>
      opts?.keyPrefix ? `${opts.keyPrefix}.${key}` : key,
    i18n: { changeLanguage: () => new Promise(() => {}) }
  }),
  Trans: ({ children }: { children?: React.ReactNode }) => children ?? null
}));

vi.mock('../components/LinuxVersion', () => ({
  LinuxVersion: () => null
}));

vi.mock('../edit-hardware/EditHardwareConfirmModal', () => ({
  EditHardwareConfirmModal: () => null
}));

const noopStore = createStore(() => ({}));

function renderHardwareTab(universeData: Universe) {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } }
  });
  return render(
    <ThemeProvider theme={mainTheme}>
      <Provider store={noopStore}>
        <QueryClientProvider client={queryClient}>
          <EditUniverseContext.Provider
            value={{
              activeTab: EditUniverseTabs.HARDWARE,
              universeData,
              providerRegions: makeProviderRegions()
            }}
          >
            <HardwareTab />
          </EditUniverseContext.Provider>
        </QueryClientProvider>
      </Provider>
    </ThemeProvider>
  );
}

describe('HardwareTab', () => {
  it('renders non-dedicated view with one cluster instance edit control when no dedicated nodes', () => {
    renderHardwareTab(makeNonGeoUniverse());
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(1);
  });

  it('renders non-dedicated view with cluster and read replica instance cards', () => {
    renderHardwareTab(makeNonGeoUniverseWithReadReplicaPlacementSpec());
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(2);
  });

  it('renders dedicated master/tserver view with two primary edit controls', () => {
    renderHardwareTab(makeNonGeoUniverseWithDedicatedNodeDetails());
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(2);
  });

  it('renders dedicated view with read replica hardware card when ASYNC exists', () => {
    const u = makeNonGeoUniverseWithReadReplicaPlacementSpec();
    const withDedicated: Universe = {
      ...u,
      info: {
        ...u.info!,
        node_details_set: [
          { node_uuid: 'n1', az_uuid: 'az-uuid-1', dedicated_to: NodeDetailsDedicatedTo.TSERVER },
          { node_uuid: 'n2', az_uuid: 'az-uuid-1', dedicated_to: NodeDetailsDedicatedTo.MASTER }
        ]
      }
    } as Universe;
    renderHardwareTab(withDedicated);
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(3);
  });
});
