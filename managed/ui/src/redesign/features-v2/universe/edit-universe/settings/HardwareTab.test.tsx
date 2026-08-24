import { render, screen } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { beforeEach, describe, expect, it, vi } from 'vitest';
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
  makeGeoK8sUniverse,
  makeK8sUniverse,
  makeProviderRegions
} from '../__fixtures__/editUniverseFixtures';

const runtimeConfigState = vi.hoisted(() => ({
  useK8CustomResources: true
}));

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

vi.mock('../../create-universe/helpers/utils', () => ({
  useRuntimeConfigValues: () => ({
    useK8CustomResources: runtimeConfigState.useK8CustomResources,
    osPatchingEnabled: false,
    maxVolumeCount: 16,
    canUseSpotInstance: false,
    isRuntimeConfigLoading: false,
    isProviderRuntimeConfigLoading: false,
    ebsVolumeEnabled: false
  })
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
  beforeEach(() => {
    runtimeConfigState.useK8CustomResources = true;
  });

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

  it('renders K8s custom-resource universe as dedicated T-Server and Master cards', () => {
    renderHardwareTab(makeK8sUniverse(true));
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(2);
    expect(screen.getAllByText('cpuCores')).toHaveLength(2);
    expect(screen.getAllByText('memory')).toHaveLength(2);
  });

  it('renders K8s instance-type universe as a cluster instance card when custom resources are absent', () => {
    runtimeConfigState.useK8CustomResources = false;
    renderHardwareTab(makeK8sUniverse(false));
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(1);
    expect(screen.getByText('yb-k8s-small')).toBeInTheDocument();
  });

  it('renders geo-partitioned K8s custom-resource universe without per-partition hardware cards', () => {
    renderHardwareTab(makeGeoK8sUniverse(true));
    expect(screen.getAllByTestId('edit-placement-edit-button')).toHaveLength(2);
  });
});
