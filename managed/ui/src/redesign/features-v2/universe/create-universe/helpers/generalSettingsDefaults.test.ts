import { describe, expect, it } from 'vitest';
import { ProviderStatus } from '@app/components/configRedesign/providerRedesign/constants';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ReleaseState } from '@app/redesign/features/releases/components/dtos';
import {
  buildGeneralSettingsDefaults,
  getDefaultCloudType,
  getDefaultProviderForCloud,
  getLatestStableDbVersion,
  getReadyProviders,
  isProviderValidForCloud,
  ProviderListItem,
  resolveProviderOnCloudSync,
  transformDbVersionQueryData
} from './generalSettingsDefaults';

function provider(
  overrides: Pick<ProviderListItem, 'uuid' | 'code' | 'name'> & Partial<ProviderListItem>
): ProviderListItem {
  return {
    usabilityState: ProviderStatus.READY,
    ...overrides
  };
}

const awsAlpha = provider({ uuid: 'aws-1', code: CloudType.aws, name: 'aws-alpha' });
const awsBeta = provider({ uuid: 'aws-2', code: CloudType.aws, name: 'aws-beta' });
const gcpOne = provider({ uuid: 'gcp-1', code: CloudType.gcp, name: 'gcp-one' });
const k8sUpdating = provider({
  uuid: 'k8s-1',
  code: CloudType.kubernetes,
  name: 'k8s-updating',
  usabilityState: ProviderStatus.UPDATING
});

const providers = [awsBeta, gcpOne, awsAlpha, k8sUpdating];

describe('getReadyProviders', () => {
  it('returns ready providers for a cloud, sorted by code then name', () => {
    expect(getReadyProviders(providers, CloudType.aws).map((item) => item.uuid)).toEqual([
      'aws-1',
      'aws-2'
    ]);
  });

  it('excludes providers that are not READY', () => {
    expect(getReadyProviders(providers, CloudType.kubernetes)).toEqual([]);
  });
});

describe('getDefaultCloudType', () => {
  it('falls back to AWS when no ready providers exist', () => {
    expect(getDefaultCloudType([])).toBe(CloudType.aws);
  });

  it('selects the configured cloud when only one cloud has ready providers', () => {
    expect(getDefaultCloudType([gcpOne])).toBe(CloudType.gcp);
  });
});

describe('resolveProviderOnCloudSync', () => {
  it('selects the first ready provider on first sync when none is selected', () => {
    const result = resolveProviderOnCloudSync({
      currentCloud: CloudType.aws,
      currentProvider: undefined,
      providers
    });

    expect(result.shouldApply).toBe(true);
    expect(result.provider?.uuid).toBe('aws-1');
  });

  it('keeps a valid provider on first sync', () => {
    const result = resolveProviderOnCloudSync({
      currentCloud: CloudType.aws,
      currentProvider: awsBeta,
      providers
    });

    expect(result.shouldApply).toBe(false);
  });

  it('clears the provider when switching to a cloud with no ready providers', () => {
    const result = resolveProviderOnCloudSync({
      previousCloud: CloudType.aws,
      currentCloud: CloudType.kubernetes,
      currentProvider: awsAlpha,
      providers
    });

    expect(result.shouldApply).toBe(true);
    expect(result.provider).toBeNull();
  });

  it('selects the first provider after switching from an empty cloud list', () => {
    const afterEmptyCloud = resolveProviderOnCloudSync({
      previousCloud: CloudType.aws,
      currentCloud: CloudType.kubernetes,
      currentProvider: awsAlpha,
      providers
    });

    const afterPopulatedCloud = resolveProviderOnCloudSync({
      previousCloud: CloudType.kubernetes,
      currentCloud: CloudType.gcp,
      currentProvider: afterEmptyCloud.provider,
      providers
    });

    expect(afterPopulatedCloud.shouldApply).toBe(true);
    expect(afterPopulatedCloud.provider?.uuid).toBe('gcp-1');
    expect(
      isProviderValidForCloud(afterPopulatedCloud.provider, CloudType.gcp, providers)
    ).toBe(true);
  });

  it('does not overwrite the provider when the cloud does not change', () => {
    const result = resolveProviderOnCloudSync({
      previousCloud: CloudType.gcp,
      currentCloud: CloudType.gcp,
      currentProvider: undefined,
      providers
    });

    expect(result.shouldApply).toBe(false);
  });
});

describe('getDefaultProviderForCloud', () => {
  it('returns null when the cloud has no ready providers', () => {
    expect(getDefaultProviderForCloud(providers, CloudType.kubernetes)).toBeNull();
  });
});

describe('getLatestStableDbVersion', () => {
  it('returns the latest active stable version', () => {
    const transformed = transformDbVersionQueryData([
      { version: '2.21.0.0-b1', state: ReleaseState.ACTIVE },
      { version: '2.20.1.0-b1', state: ReleaseState.ACTIVE },
      { version: '2024.2.0.0-b1', state: ReleaseState.ACTIVE },
      { version: '2024.1.0.0-b1', state: ReleaseState.DISABLED }
    ] as any);

    expect(getLatestStableDbVersion(transformed)).toBe('2024.2.0.0-b1');
  });
});

describe('buildGeneralSettingsDefaults', () => {
  it('fills universe name, cloud, provider, and db version', () => {
    const transformed = transformDbVersionQueryData([
      { version: '2024.2.0.0-b1', state: ReleaseState.ACTIVE }
    ] as any);

    const defaults = buildGeneralSettingsDefaults(providers, transformed);

    expect(defaults.universeName).toBeTruthy();
    expect(defaults.cloud).toBe(getDefaultCloudType(providers));
    expect(defaults.providerConfiguration?.uuid).toBe(
      getDefaultProviderForCloud(providers, defaults.cloud)?.uuid
    );
    expect(defaults.databaseVersion).toBe('2024.2.0.0-b1');
  });

  it('preserves values the user already selected', () => {
    const defaults = buildGeneralSettingsDefaults(providers, undefined, {
      universeName: 'my-universe',
      cloud: CloudType.gcp,
      providerConfiguration: getDefaultProviderForCloud(providers, CloudType.gcp)!,
      databaseVersion: '2.20.1.0-b1'
    });

    expect(defaults).toMatchObject({
      universeName: 'my-universe',
      cloud: CloudType.gcp,
      providerConfiguration: expect.objectContaining({ uuid: 'gcp-1' }),
      databaseVersion: '2.20.1.0-b1'
    });
  });
});
