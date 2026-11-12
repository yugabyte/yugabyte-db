import { describe, expect, it } from 'vitest';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderType } from '../steps/general-settings/dtos';
import {
  availabilityZonesByProviderFromNodes,
  buildNodesByProvider,
  flattenAvailabilityZonesByProvider,
  getNodeCountFromByProvider,
  getMasterCountsByProvider,
  getNodeCountsByProvider,
  unflattenAvailabilityZonesByProvider
} from './nodesByProvider';

function provider(uuid: string): ProviderType {
  return { uuid, code: CloudType.aws } as ProviderType;
}

describe('nodesByProvider helpers', () => {
  it('builds and hydrates nodesByProvider with only availabilityZones', () => {
    const providers = [provider('a'), provider('b')];
    const byProvider = {
      a: { r1: [{ name: 'z0', uuid: 'z0', nodeCount: 1, preffered: 1 }] },
      b: { r2: [{ name: 'z1', uuid: 'z1', nodeCount: 2, preffered: -1 }] }
    };
    const nodes = buildNodesByProvider(byProvider, providers);
    expect(nodes.a.availabilityZones.r1[0].name).toBe('z0');
    expect(nodes.b.useDedicatedNodes).toBe(false);
    expect(availabilityZonesByProviderFromNodes(nodes, providers).b.r2[0].nodeCount).toBe(2);
  });

  it('flattens and unflattens with collision-safe keys', () => {
    const byProvider = {
      a: { 'us-east-1': [{ name: 'az1', uuid: '1', nodeCount: 1, preffered: -1 }] },
      b: { 'us-east-1': [{ name: 'az2', uuid: '2', nodeCount: 3, preffered: -1 }] }
    };
    const flat = flattenAvailabilityZonesByProvider(byProvider);
    expect(Object.keys(flat).sort()).toEqual(['a:us-east-1', 'b:us-east-1']);
    expect(getNodeCountFromByProvider(byProvider)).toBe(4);
    expect(getNodeCountsByProvider(byProvider)).toEqual({ a: 1, b: 3 });
    expect(
      unflattenAvailabilityZonesByProvider(flat, [provider('a'), provider('b')])
    ).toEqual(byProvider);
  });

  it('places one master per AZ across providers, wrapping when rf exceeds AZs', () => {
    const providers = [provider('a'), provider('b')];
    const zone = (name: string, nodeCount = 1) => ({ name, uuid: name, nodeCount, preffered: -1 });
    const byProvider = {
      a: { r1: [zone('z0', 2)] },
      b: { r2: [zone('z1')], r3: [zone('z2'), zone('z3', 0)] }
    };
    expect(getMasterCountsByProvider(byProvider, providers, 3)).toEqual({ a: 1, b: 2 });
    expect(getMasterCountsByProvider(byProvider, providers, 1)).toEqual({ a: 1 });
    expect(getMasterCountsByProvider(byProvider, providers, 5)).toEqual({ a: 2, b: 3 });
    expect(getMasterCountsByProvider({}, providers, 3)).toEqual({});
  });
});
