import React from 'react';
import { fireEvent, render, screen } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { mainTheme } from '@app/redesign/theme/mainTheme';
import { EditUniverseContext, EditUniverseTabs } from '../EditUniverseContext';
import { EditHardwareConfirmModal } from './EditHardwareConfirmModal';
import { ClusterSpecClusterType, type Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import type { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import {
  FIXTURE_ASYNC_CLUSTER_UUID,
  FIXTURE_PRIMARY_CLUSTER_UUID,
  makeGeoK8sUniverse,
  makeK8sUniverse,
  makeK8sUniverseWithReadReplica,
  makeNonGeoUniverse,
  makeNonGeoUniverseWithDedicatedNodeDetails,
  makeNonGeoUniverseWithReadReplicaPlacementSpec,
  makeProviderRegions,
  makeSingleGeoPartitionUniverse
} from '../__fixtures__/editUniverseFixtures';

const mockState = vi.hoisted(() => ({
  editMutate: vi.fn(),
  resizeMutate: vi.fn(),
  settings: {} as InstanceSettingProps
}));

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string, opts?: Record<string, string>) =>
      opts?.keyPrefix ? `${opts.keyPrefix}.${key}` : key,
    i18n: { changeLanguage: () => new Promise(() => {}) }
  }),
  Trans: ({ children }: { children?: React.ReactNode }) => children ?? null
}));

vi.mock('@yugabyte-ui-library/core', async (importOriginal) => {
  const actual = await importOriginal<typeof import('@yugabyte-ui-library/core')>();
  return {
    ...actual,
    YBCheckbox: ({ checked, label, onChange }: any) => (
      <label>
        <input
          type="checkbox"
          checked={checked}
          onChange={(event) => onChange?.(event, event.target.checked)}
        />
        {label}
      </label>
    ),
    yba: {
      ...actual.yba,
      YBModal: ({ open, children, onSubmit, submitLabel }: any) =>
        open ? (
          <div>
            {children}
            <button onClick={onSubmit}>{submitLabel}</button>
          </div>
        ) : null
    }
  };
});

vi.mock('../../../../../v2/api/universe/universe', () => ({
  useEditUniverse: () => ({
    mutate: mockState.editMutate,
    isLoading: false
  }),
  useResizeNodes: () => ({
    mutate: mockState.resizeMutate,
    isLoading: false
  })
}));

vi.mock('../../../../features/universe/universe-form/utils/api', () => ({
  QUERY_KEY: { getInstanceTypes: 'getInstanceTypes' },
  api: {
    getInstanceTypes: vi.fn().mockResolvedValue([])
  }
}));

vi.mock('../hooks/useEditUniverseTaskHandler', () => ({
  useEditUniverseTaskHandler: () => vi.fn()
}));

vi.mock('../../create-universe/helpers/ToastUtils', () => ({
  useYBToast: () => ({
    info: vi.fn(),
    success: vi.fn(),
    error: vi.fn(),
    warn: vi.fn(),
    inProgress: vi.fn()
  })
}));

vi.mock('./ReviewHardwareChangesModal', () => ({
  hardwareReviewSectionHasVisibleChanges: (current: Record<string, unknown>, next: Record<string, unknown>) =>
    JSON.stringify(current) !== JSON.stringify(next),
  ReviewHardwareChangesModal: ({
    visible,
    onConfirm
  }: {
    visible: boolean;
    onConfirm: (delaySeconds: number) => void;
  }) =>
    visible ? (
      <button data-testid="confirm-review" onClick={() => onConfirm(1)}>
        confirm
      </button>
    ) : null
}));

vi.mock('../../create-universe/steps', async () => {
  const ReactActual = await vi.importActual<typeof import('react')>('react');
  const { CreateUniverseContext } = await vi.importActual<
    typeof import('../../create-universe/CreateUniverseContext')
  >('../../create-universe/CreateUniverseContext');

  return {
    InstanceSettings: ReactActual.forwardRef((_props, ref) => {
      const [, methods] = ReactActual.useContext(CreateUniverseContext) as any;
      ReactActual.useImperativeHandle(ref, () => ({
        onNext: () => {
          methods.saveInstanceSettings(mockState.settings);
          methods.moveToNextPage();
        },
        onPrev: vi.fn()
      }));
      return ReactActual.createElement('div', { 'data-testid': 'mock-instance-settings' });
    })
  };
});

const noopStore = createStore(() => ({
  customer: { currentCustomer: { data: { uuid: 'customer-uuid' } } }
}));

const vmSettings = (overrides: Partial<InstanceSettingProps> = {}) =>
  ({
    arch: 'x86_64',
    imageBundleUUID: null,
    useSpotInstance: false,
    instanceType: 'c5.2xlarge',
    masterInstanceType: 'c5.2xlarge',
    keepMasterTserverSame: false,
    deviceInfo: {
      volumeSize: 100,
      numVolumes: 1,
      diskIops: 3000,
      throughput: 125,
      storageClass: 'standard',
      storageType: 'GP3'
    },
    masterDeviceInfo: {
      volumeSize: 100,
      numVolumes: 1,
      diskIops: 3000,
      throughput: 125,
      storageClass: 'standard',
      storageType: 'GP3'
    },
    ...overrides
  } as InstanceSettingProps);

const k8sSettings = (overrides: Partial<InstanceSettingProps> = {}) =>
  ({
    arch: 'x86_64',
    imageBundleUUID: null,
    useSpotInstance: false,
    instanceType: null,
    masterInstanceType: null,
    keepMasterTserverSame: false,
    deviceInfo: {
      volumeSize: 100,
      numVolumes: 1,
      diskIops: null,
      throughput: null,
      storageClass: 'standard',
      storageType: null
    },
    masterDeviceInfo: {
      volumeSize: 100,
      numVolumes: 1,
      diskIops: null,
      throughput: null,
      storageClass: 'standard',
      storageType: null
    },
    tserverK8SNodeResourceSpec: { cpuCoreCount: 3, memoryGib: 5 },
    masterK8SNodeResourceSpec: { cpuCoreCount: 2, memoryGib: 3 },
    ...overrides
  } as InstanceSettingProps);

function renderModal(
  universeData: Universe,
  props: Partial<React.ComponentProps<typeof EditHardwareConfirmModal>> = {}
) {
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
            <EditHardwareConfirmModal
              visible
              mode="cluster"
              onSubmit={vi.fn()}
              onHide={vi.fn()}
              {...props}
            />
          </EditUniverseContext.Provider>
        </QueryClientProvider>
      </Provider>
    </ThemeProvider>
  );
}

function submitAndConfirm() {
  fireEvent.click(screen.getByText('submitLabel'));
  fireEvent.click(screen.getByTestId('confirm-review'));
}

const getFirstEditPayload = () => mockState.editMutate.mock.calls[0][0].data.clusters[0];
const getFirstResizePayload = () => mockState.resizeMutate.mock.calls[0][0].data.clusters[0];

describe('EditHardwareConfirmModal payloads', () => {
  beforeEach(() => {
    mockState.editMutate.mockReset();
    mockState.resizeMutate.mockReset();
    mockState.settings = vmSettings();
  });

  it('uses resize-nodes for VM non-dedicated instance/storage edits without placement fields', () => {
    renderModal(makeNonGeoUniverse());
    submitAndConfirm();

    const payload = getFirstResizePayload();
    expect(payload.uuid).toBe(FIXTURE_PRIMARY_CLUSTER_UUID);
    expect(payload.node_spec.instance_type).toBe('c5.2xlarge');
    expect(payload).not.toHaveProperty('placement_spec');
    expect(payload).not.toHaveProperty('partitions_spec');
    expect(mockState.editMutate).not.toHaveBeenCalled();
  });

  it('uses edit-universe for VM non-dedicated volume-count edits', () => {
    mockState.settings = vmSettings({
      deviceInfo: { ...vmSettings().deviceInfo!, numVolumes: 2 }
    });

    renderModal(makeNonGeoUniverse());
    submitAndConfirm();

    const payload = getFirstEditPayload();
    expect(payload.node_spec.storage_spec.num_volumes).toBe(2);
    expect(payload).not.toHaveProperty('placement_spec');
    expect(payload).not.toHaveProperty('partitions_spec');
    expect(mockState.resizeMutate).not.toHaveBeenCalled();
  });

  it('keeps geo-partitioned VM hardware edits cluster-level only', () => {
    renderModal(makeSingleGeoPartitionUniverse());
    submitAndConfirm();

    const payload = getFirstResizePayload();
    expect(payload.uuid).toBe(FIXTURE_PRIMARY_CLUSTER_UUID);
    expect(payload).not.toHaveProperty('placement_spec');
    expect(payload).not.toHaveProperty('partitions_spec');
  });

  it('emits only tserver changes for dedicated VM tserver mode', () => {
    renderModal(makeNonGeoUniverseWithDedicatedNodeDetails(), { mode: 'tserver' });
    submitAndConfirm();

    const payload = getFirstResizePayload();
    expect(payload.node_spec.tserver.instance_type).toBe('c5.2xlarge');
    expect(payload.node_spec).not.toHaveProperty('master');
  });

  it('emits only master changes for dedicated VM master mode', () => {
    mockState.settings = vmSettings({
      instanceType: 'c5.xlarge',
      masterInstanceType: 'c5.4xlarge'
    });

    renderModal(makeNonGeoUniverseWithDedicatedNodeDetails(), { mode: 'master' });
    submitAndConfirm();

    const payload = getFirstResizePayload();
    expect(payload.node_spec.master.instance_type).toBe('c5.4xlarge');
    expect(payload.node_spec).not.toHaveProperty('tserver');
  });

  it('targets the ASYNC cluster and edit-universe for VM read replica hardware edits', () => {
    renderModal(makeNonGeoUniverseWithReadReplicaPlacementSpec(), {
      mode: 'readReplica',
      clusterType: ClusterSpecClusterType.ASYNC
    });
    submitAndConfirm();

    const payload = getFirstEditPayload();
    expect(payload.uuid).toBe(FIXTURE_ASYNC_CLUSTER_UUID);
    expect(payload.num_nodes).toBe(1);
    expect(payload.node_spec.instance_type).toBe('c5.2xlarge');
    expect(mockState.resizeMutate).not.toHaveBeenCalled();
  });

  it('routes K8s custom-resource edits through edit-universe with tserver and master specs', () => {
    mockState.settings = k8sSettings();

    renderModal(makeK8sUniverse(true));
    submitAndConfirm();

    const payload = getFirstEditPayload();
    expect(payload.node_spec.k8s_tserver_resource_spec).toEqual({
      cpu_core_count: 3,
      memory_gib: 5
    });
    expect(payload.node_spec.k8s_master_resource_spec).toEqual({
      cpu_core_count: 2,
      memory_gib: 3
    });
    expect(payload.node_spec.storage_spec.storage_class).toBe('standard');
    expect(mockState.resizeMutate).not.toHaveBeenCalled();
  });

  it('routes K8s master-only edits with the master change and unchanged tserver fallback', () => {
    mockState.settings = k8sSettings({
      tserverK8SNodeResourceSpec: { cpuCoreCount: 2, memoryGib: 4 },
      masterK8SNodeResourceSpec: { cpuCoreCount: 2.5, memoryGib: 3 }
    });

    renderModal(makeK8sUniverse(true), { mode: 'master' });
    submitAndConfirm();

    const payload = getFirstEditPayload();
    expect(payload.node_spec.k8s_master_resource_spec).toEqual({
      cpu_core_count: 2.5,
      memory_gib: 3
    });
    expect(payload.node_spec.k8s_tserver_resource_spec).toEqual({
      cpu_core_count: 2,
      memory_gib: 4
    });
  });

  it('includes both K8s resource specs for dedicated K8s tserver-only edits', () => {
    mockState.settings = k8sSettings({
      tserverK8SNodeResourceSpec: { cpuCoreCount: 3, memoryGib: 5 },
      masterK8SNodeResourceSpec: { cpuCoreCount: 2, memoryGib: 3 }
    });

    renderModal(makeK8sUniverse(true), { mode: 'tserver' });
    submitAndConfirm();

    const payload = getFirstEditPayload();
    expect(payload.node_spec.k8s_tserver_resource_spec).toEqual({
      cpu_core_count: 3,
      memory_gib: 5
    });
    expect(payload.node_spec.k8s_master_resource_spec).toEqual({
      cpu_core_count: 2,
      memory_gib: 3
    });
  });

  it('emits master resize payload when keepMasterTserverSame is checked with no field delta', () => {
    const matchingDeviceInfo = {
      ...vmSettings().deviceInfo!,
      volumeSize: 100,
      numVolumes: 1,
      diskIops: 3000,
      throughput: 125
    };
    mockState.settings = vmSettings({
      instanceType: 'c5.xlarge',
      masterInstanceType: 'c5.xlarge',
      keepMasterTserverSame: true,
      deviceInfo: matchingDeviceInfo,
      masterDeviceInfo: matchingDeviceInfo
    });

    renderModal(makeNonGeoUniverseWithDedicatedNodeDetails(), { mode: 'master' });
    submitAndConfirm();

    const payload = getFirstResizePayload();
    expect(payload.node_spec.master).toEqual({
      instance_type: 'c5.xlarge',
      storage_spec: {
        volume_size: 100,
        disk_iops: 3000,
        throughput: 125
      }
    });
    expect(payload.node_spec).not.toHaveProperty('tserver');
  });

  it('routes K8s master-only provisioned throughput edits through edit-universe', () => {
    mockState.settings = k8sSettings({
      masterDeviceInfo: {
        ...k8sSettings().masterDeviceInfo!,
        volumeSize: 200,
        numVolumes: 2
      }
    });

    renderModal(makeK8sUniverse(true), { mode: 'master' });
    submitAndConfirm();

    const payload = getFirstEditPayload();
    expect(payload.node_spec.master?.storage_spec).toEqual({
      volume_size: 200,
      num_volumes: 2,
      storage_class: 'standard'
    });
    expect(mockState.resizeMutate).not.toHaveBeenCalled();
  });

  it('keeps geo-partitioned K8s hardware edits cluster-level only', () => {
    mockState.settings = k8sSettings();

    renderModal(makeGeoK8sUniverse(true), { mode: 'tserver' });
    submitAndConfirm();

    const payload = getFirstEditPayload();
    expect(payload.uuid).toBe(FIXTURE_PRIMARY_CLUSTER_UUID);
    expect(payload.node_spec.k8s_tserver_resource_spec).toEqual({
      cpu_core_count: 3,
      memory_gib: 5
    });
    expect(payload.node_spec.k8s_master_resource_spec).toEqual({
      cpu_core_count: 2,
      memory_gib: 3
    });
    expect(payload).not.toHaveProperty('placement_spec');
    expect(payload).not.toHaveProperty('partitions_spec');
  });

  it('targets the ASYNC cluster and edit-universe for K8s read replica hardware edits', () => {
    mockState.settings = k8sSettings({
      tserverK8SNodeResourceSpec: { cpuCoreCount: 2, memoryGib: 4 }
    });

    renderModal(makeK8sUniverseWithReadReplica(true), {
      mode: 'readReplica',
      clusterType: ClusterSpecClusterType.ASYNC
    });
    submitAndConfirm();

    const payload = getFirstEditPayload();
    expect(payload.uuid).toBe(FIXTURE_ASYNC_CLUSTER_UUID);
    expect(payload.node_spec.k8s_tserver_resource_spec).toEqual({
      cpu_core_count: 2,
      memory_gib: 4
    });
    expect(payload.node_spec).not.toHaveProperty('k8s_master_resource_spec');
  });
});
