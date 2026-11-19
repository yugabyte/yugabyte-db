import { describe, expect, it } from 'vitest';
import { CloudType, DeviceInfo } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  areAllProvidersInstanceConfigured,
  defaultInstanceSettings,
  isMasterInstanceConfigured,
  isTserverInstanceConfigured,
  providerUuidSetKey,
  stripMasterInstanceFields
} from './instanceByProvider';
import { ProviderType } from '../steps/general-settings/dtos';
import { InstanceSettingProps } from '../steps/hardware-settings/dtos';

const provider = (uuid: string, code = CloudType.aws): ProviderType =>
  ({ uuid, code, name: uuid } as ProviderType);

const configuredTserver = (): InstanceSettingProps => ({
  ...defaultInstanceSettings(),
  instanceType: 'c5.large',
  deviceInfo: {
    numVolumes: 1,
    volumeSize: 100,
    storageType: null,
    mountPoints: null,
    diskIops: null,
    throughput: null,
    storageClass: null
  } as unknown as DeviceInfo
});

describe('instanceByProvider helpers', () => {
  it('providerUuidSetKey is order-independent', () => {
    expect(providerUuidSetKey([provider('b'), provider('a')])).toBe(
      providerUuidSetKey([provider('a'), provider('b')])
    );
  });

  it('isTserverInstanceConfigured requires instance type + device', () => {
    expect(isTserverInstanceConfigured(defaultInstanceSettings(), CloudType.aws)).toBe(false);
    expect(isTserverInstanceConfigured(configuredTserver(), CloudType.aws)).toBe(true);
  });

  it('isMasterInstanceConfigured treats keep-same as configured when tserver is set', () => {
    const settings = { ...configuredTserver(), keepMasterTserverSame: true };
    expect(isMasterInstanceConfigured(settings, true, CloudType.aws)).toBe(true);
    expect(
      isMasterInstanceConfigured(
        { ...configuredTserver(), keepMasterTserverSame: false },
        true,
        CloudType.aws
      )
    ).toBe(false);
  });

  it('areAllProvidersInstanceConfigured gates Next', () => {
    const providers = [provider('a'), provider('b')];
    expect(areAllProvidersInstanceConfigured(providers, {}, false)).toBe(false);
    expect(
      areAllProvidersInstanceConfigured(
        providers,
        { a: configuredTserver(), b: configuredTserver() },
        false
      )
    ).toBe(true);
  });

  it('stripMasterInstanceFields clears master fields', () => {
    const stripped = stripMasterInstanceFields({
      ...configuredTserver(),
      keepMasterTserverSame: false,
      masterInstanceType: 'c5.xlarge',
      masterDeviceInfo: configuredTserver().deviceInfo
    });
    expect(stripped.masterInstanceType).toBeNull();
    expect(stripped.masterDeviceInfo).toBeNull();
    expect(stripped.keepMasterTserverSame).toBe(true);
  });
});
