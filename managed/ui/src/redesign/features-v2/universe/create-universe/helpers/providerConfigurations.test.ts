import { describe, expect, it } from 'vitest';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderType } from '../steps/general-settings/dtos';
import {
  cloudCodes,
  crossedProviderCountBoundary,
  getPrimaryProvider,
  isMultiProvider,
  MAX_PROVIDER_CONFIGURATIONS,
  normalizeProviderConfigurations,
  primaryCloudCode,
  providersOrderedByClouds
} from './providerConfigurations';

function provider(uuid: string, code: CloudType = CloudType.aws): ProviderType {
  return { uuid, code } as ProviderType;
}

describe('normalizeProviderConfigurations', () => {
  it('drops nulls, duplicates, and caps at max', () => {
    const a = provider('a');
    const b = provider('b');
    const c = provider('c');
    const d = provider('d');
    expect(
      normalizeProviderConfigurations([a, null, a, b, c, d]).map((p) => p.uuid)
    ).toEqual(['a', 'b', 'c']);
    expect(MAX_PROVIDER_CONFIGURATIONS).toBe(3);
  });
});

describe('getPrimaryProvider / isMultiProvider', () => {
  it('reads index 0 and treats length>=2 as multi', () => {
    expect(getPrimaryProvider({ providerConfigurations: [provider('a')] })?.uuid).toBe('a');
    expect(isMultiProvider({ providerConfigurations: [provider('a')] })).toBe(false);
    expect(
      isMultiProvider({ providerConfigurations: [provider('a'), provider('b')] })
    ).toBe(true);
  });
});

describe('crossedProviderCountBoundary', () => {
  it('fires only when crossing 1 ↔ 2+', () => {
    expect(crossedProviderCountBoundary(1, 2)).toBe(true);
    expect(crossedProviderCountBoundary(2, 1)).toBe(true);
    expect(crossedProviderCountBoundary(2, 3)).toBe(false);
    expect(crossedProviderCountBoundary(1, 1)).toBe(false);
    expect(crossedProviderCountBoundary(0, 1)).toBe(false);
  });
});

describe('cloudCodes / primaryCloudCode / providersOrderedByClouds', () => {
  it('normalizes cloud string or array', () => {
    expect(cloudCodes(CloudType.aws)).toEqual([CloudType.aws]);
    expect(cloudCodes([CloudType.aws, CloudType.gcp])).toEqual([CloudType.aws, CloudType.gcp]);
    expect(cloudCodes(undefined)).toEqual([]);
  });

  it('prefers primary provider code over cloud field', () => {
    expect(
      primaryCloudCode({
        cloud: [CloudType.gcp, CloudType.aws],
        providerConfigurations: [provider('a', CloudType.aws), provider('g', CloudType.gcp)]
      })
    ).toBe(CloudType.aws);
  });

  it('orders providers by cloud selection and keeps one per code', () => {
    const aws = provider('aws-1', CloudType.aws);
    const gcp = provider('gcp-1', CloudType.gcp);
    expect(
      providersOrderedByClouds([CloudType.gcp, CloudType.aws], [aws, gcp]).map((p) => p.uuid)
    ).toEqual(['gcp-1', 'aws-1']);
  });
});
