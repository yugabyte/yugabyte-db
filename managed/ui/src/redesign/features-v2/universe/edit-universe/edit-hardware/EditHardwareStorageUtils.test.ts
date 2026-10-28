import { describe, expect, it } from 'vitest';
import { ResizeUpdateOption } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import type { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import {
  getK8sResizeOptions,
  onlyVolumeSizeIncreased,
  toClusterStorageSpec,
  type NormalizedStorage
} from './EditHardwareStorageUtils';

const baseStorage = (overrides: Partial<NormalizedStorage> = {}): NormalizedStorage => ({
  volumeSize: 100,
  numVolumes: 1,
  diskIops: null,
  throughput: null,
  storageClass: 'standard',
  storageType: null,
  mountPoints: null,
  ...overrides
});

const cluster = {
  node_spec: {
    storage_spec: { volume_size: 100, num_volumes: 1, storage_class: 'standard' },
    k8s_tserver_resource_spec: { cpu_core_count: 2, memory_gib: 4 },
    k8s_master_resource_spec: { cpu_core_count: 1, memory_gib: 2 },
    dedicated_nodes: true
  }
} as any;

const settings = (overrides: Partial<InstanceSettingProps> = {}): InstanceSettingProps =>
  ({
    instanceType: null,
    masterInstanceType: null,
    keepMasterTserverSame: false,
    deviceInfo: {
      volumeSize: 100,
      numVolumes: 1,
      storageClass: 'standard'
    },
    masterDeviceInfo: {
      volumeSize: 100,
      numVolumes: 1,
      storageClass: 'standard'
    },
    tserverK8SNodeResourceSpec: { cpuCoreCount: 2, memoryGib: 4 },
    masterK8SNodeResourceSpec: { cpuCoreCount: 1, memoryGib: 2 },
    ...overrides
  } as InstanceSettingProps);

describe('onlyVolumeSizeIncreased', () => {
  it('true only when volume grew and nothing else changed', () => {
    expect(onlyVolumeSizeIncreased(baseStorage(), baseStorage({ volumeSize: 200 }))).toBe(true);
    expect(onlyVolumeSizeIncreased(baseStorage(), baseStorage({ volumeSize: 50 }))).toBe(false);
    expect(
      onlyVolumeSizeIncreased(baseStorage(), baseStorage({ volumeSize: 200, numVolumes: 2 }))
    ).toBe(false);
  });
});

describe('toClusterStorageSpec', () => {
  const currentSpec = {
    volume_size: 100,
    num_volumes: 1,
    disk_iops: 3000,
    throughput: 125,
    storage_type: 'GP3' as const
  };

  it('omits cleared IOPS/throughput when switching to a type that does not support them', () => {
    // Form clears these to null on GP3→GP2 / IO1→IO2 (throughput) etc.
    const spec = toClusterStorageSpec(
      {
        volumeSize: 100,
        numVolumes: 1,
        diskIops: null,
        throughput: null,
        storageType: 'GP2'
      },
      currentSpec
    );

    expect(spec.storage_type).toBe('GP2');
    expect(spec).not.toHaveProperty('disk_iops');
    expect(spec).not.toHaveProperty('throughput');
  });

  it('keeps IOPS when the new type supports it but clears unsupported throughput', () => {
    const spec = toClusterStorageSpec(
      {
        volumeSize: 100,
        numVolumes: 1,
        diskIops: 1000,
        throughput: null,
        storageType: 'IO1'
      },
      currentSpec
    );

    expect(spec.storage_type).toBe('IO1');
    expect(spec.disk_iops).toBe(1000);
    expect(spec).not.toHaveProperty('throughput');
  });

  it('preserves current IOPS/throughput when deviceInfo is absent', () => {
    const spec = toClusterStorageSpec(null, currentSpec);
    expect(spec.disk_iops).toBe(3000);
    expect(spec.throughput).toBe(125);
  });
});

describe('getK8sResizeOptions', () => {
  const opts = (s: InstanceSettingProps) =>
    getK8sResizeOptions({
      settings: s,
      targetCluster: cluster,
      dedicatedNodes: true,
      mode: 'cluster',
      currentTserverK8s: { cpuCoreCount: 2, memoryGib: 4 },
      currentMasterK8s: { cpuCoreCount: 1, memoryGib: 2 }
    });

  it('offers smart resize only for volume grow; full move for device replace / CPU for smart resize', () => {
    expect(
      opts(
        settings({
          deviceInfo: { volumeSize: 200, numVolumes: 1, storageClass: 'standard' }
        })
      )
    ).toEqual([ResizeUpdateOption.SMART_RESIZE_NON_RESTART]);

    expect(
      opts(
        settings({
          tserverK8SNodeResourceSpec: { cpuCoreCount: 3, memoryGib: 4 }
        })
      )
    ).toEqual([ResizeUpdateOption.SMART_RESIZE]);

    expect(
      opts(
        settings({
          deviceInfo: { volumeSize: 100, numVolumes: 2, storageClass: 'standard' }
        })
      )
    ).toEqual([ResizeUpdateOption.FULL_MOVE]);

    expect(
      opts(
        settings({
          deviceInfo: {
            volumeSize: 100,
            numVolumes: 1,
            storageClass: 'fast'
          }
        })
      )
    ).toEqual([ResizeUpdateOption.FULL_MOVE]);
  });
});
