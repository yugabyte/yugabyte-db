import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { InstanceSettingProps } from '../steps/hardware-settings/dtos';
import { ProviderType } from '../steps/general-settings/dtos';
import { ArchitectureType } from './constants';

export type InstanceConfigRole = 'tserver' | 'master';

export function defaultInstanceSettings(): InstanceSettingProps {
  return {
    arch: ArchitectureType.X86_64,
    imageBundleUUID: '',
    useSpotInstance: false,
    instanceType: null,
    masterInstanceType: null,
    deviceInfo: null,
    masterDeviceInfo: null,
    tserverK8SNodeResourceSpec: null,
    masterK8SNodeResourceSpec: null,
    keepMasterTserverSame: true,
    enableEbsVolumeEncryption: false,
    ebsKmsConfigUUID: null
  };
}

/** Stable key for the selected provider uuid set (order-independent). */
export function providerUuidSetKey(providers: Array<Pick<ProviderType, 'uuid'> | null | undefined>): string {
  return providers
    .map((provider) => provider?.uuid)
    .filter((uuid): uuid is string => !!uuid)
    .sort()
    .join(',');
}

function hasDevice(settings: InstanceSettingProps | null | undefined): boolean {
  const device = settings?.deviceInfo;
  return !!device && device.numVolumes != null && device.volumeSize != null;
}

function hasMasterDevice(settings: InstanceSettingProps | null | undefined): boolean {
  const device = settings?.masterDeviceInfo;
  return !!device && device.numVolumes != null && device.volumeSize != null;
}

function hasInstanceType(value: string | null | undefined): boolean {
  return value != null && value !== '';
}

/** TServer (or non-dedicated cluster) compute/storage is filled. */
export function isTserverInstanceConfigured(
  settings: InstanceSettingProps | null | undefined,
  providerCode?: string
): boolean {
  if (!settings) return false;
  if (providerCode === CloudType.kubernetes) {
    const spec = settings.tserverK8SNodeResourceSpec;
    return !!spec && spec.cpuCoreCount != null && spec.memoryGib != null && hasDevice(settings);
  }
  return hasInstanceType(settings.instanceType) && hasDevice(settings);
}

/**
 * Master HW is satisfied when dedicated is off, keep-same is on, or master fields are filled.
 */
export function isMasterInstanceConfigured(
  settings: InstanceSettingProps | null | undefined,
  useDedicatedNodes: boolean,
  providerCode?: string
): boolean {
  if (!useDedicatedNodes) return true;
  if (!settings) return false;
  if (settings.keepMasterTserverSame) return isTserverInstanceConfigured(settings, providerCode);
  if (providerCode === CloudType.kubernetes) {
    const spec = settings.masterK8SNodeResourceSpec;
    return !!spec && spec.cpuCoreCount != null && spec.memoryGib != null && hasMasterDevice(settings);
  }
  return hasInstanceType(settings.masterInstanceType) && hasMasterDevice(settings);
}

export function isProviderInstanceConfigured(
  settings: InstanceSettingProps | null | undefined,
  useDedicatedNodes: boolean,
  providerCode?: string
): boolean {
  return (
    isTserverInstanceConfigured(settings, providerCode) &&
    isMasterInstanceConfigured(settings, useDedicatedNodes, providerCode)
  );
}

export function areAllProvidersInstanceConfigured(
  providers: ProviderType[],
  instanceByProvider: Record<string, InstanceSettingProps> | null | undefined,
  useDedicatedNodes: boolean
): boolean {
  if (providers.length === 0) return false;
  return providers.every((provider) =>
    isProviderInstanceConfigured(
      instanceByProvider?.[provider.uuid],
      useDedicatedNodes,
      provider.code
    )
  );
}

export function areAllProvidersTserverConfigured(
  providers: ProviderType[],
  instanceByProvider: Record<string, InstanceSettingProps> | null | undefined
): boolean {
  if (providers.length === 0) return false;
  return providers.every((provider) =>
    isTserverInstanceConfigured(instanceByProvider?.[provider.uuid], provider.code)
  );
}

export function areAllProvidersMasterConfigured(
  providers: ProviderType[],
  instanceByProvider: Record<string, InstanceSettingProps> | null | undefined,
  useDedicatedNodes: boolean
): boolean {
  if (!useDedicatedNodes) return true;
  if (providers.length === 0) return false;
  return providers.every((provider) =>
    isMasterInstanceConfigured(
      instanceByProvider?.[provider.uuid],
      useDedicatedNodes,
      provider.code
    )
  );
}

/** Drop master-specific fields when dedicated nodes is turned off. */
export function stripMasterInstanceFields(settings: InstanceSettingProps): InstanceSettingProps {
  return {
    ...settings,
    masterInstanceType: null,
    masterDeviceInfo: null,
    masterK8SNodeResourceSpec: null,
    keepMasterTserverSame: true
  };
}

export function stripMasterFromInstanceByProvider(
  instanceByProvider: Record<string, InstanceSettingProps>
): Record<string, InstanceSettingProps> {
  const next: Record<string, InstanceSettingProps> = {};
  for (const [uuid, settings] of Object.entries(instanceByProvider)) {
    next[uuid] = stripMasterInstanceFields(settings);
  }
  return next;
}

export function archLabelKey(arch: ArchitectureType | string | undefined): string {
  if (arch === ArchitectureType.ARM64) return 'aarch64';
  return 'x86_64';
}
