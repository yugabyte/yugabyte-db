import { describe, expect, it } from 'vitest';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderType } from '../steps/general-settings/dtos';
import { REGIONS_FIELD } from '../fields/FieldNames';
import {
  buildPlacementsByProvider,
  flattenPlacementRegions,
  flattenRegionsByProvider,
  getEffectiveRegions,
  isMultiProviderPlacement,
  providerUuidsNeedingRegion,
  regionsByProviderFromPlacements,
  stampRegionProviderUuid
} from './placementsByProvider';
import { ResilienceFormMode } from '../steps/resilence-regions/dtos';
import { ENABLE_MULTI_PROVIDER_CREATE } from './providerConfigurations';

function provider(uuid: string, code: CloudType = CloudType.aws): ProviderType {
  return { uuid, code } as ProviderType;
}

function region(code: string, providerUuid?: string) {
  return {
    uuid: code,
    code,
    name: code,
    latitude: 0,
    longitude: 0,
    zones: [],
    ...(providerUuid ? { providerUuid } : {})
  } as any;
}

describe('placementsByProvider helpers', () => {
  it('stamps and flattens regionsByProvider', () => {
    const flat = flattenRegionsByProvider({
      a: [region('r1')],
      b: [region('r2')]
    });
    expect(flat.map((r) => [r.code, r.providerUuid])).toEqual([
      ['r1', 'a'],
      ['r2', 'b']
    ]);
  });

  it('builds placementsByProvider with only regions meaningful', () => {
    const placements = buildPlacementsByProvider(
      { a: [region('r1')], b: [region('r2')] },
      [provider('a'), provider('b')]
    );
    expect(placements.a[REGIONS_FIELD][0].providerUuid).toBe('a');
    expect(placements.b[REGIONS_FIELD][0].code).toBe('r2');
    expect(flattenPlacementRegions(placements).map((r) => r.code)).toEqual(['r1', 'r2']);
  });

  it('hydrates regionsByProvider from placements and prefers ByProvider for effective regions', () => {
    const providers = [provider('a'), provider('b')];
    const placements = buildPlacementsByProvider(
      { a: [region('r1')], b: [region('r2')] },
      providers
    );
    expect(regionsByProviderFromPlacements(placements, providers).a[0].code).toBe('r1');
    expect(
      getEffectiveRegions({ [REGIONS_FIELD]: [region('stale')] } as any, placements).map(
        (r) => r.code
      )
    ).toEqual(['r1', 'r2']);
    expect(getEffectiveRegions({ [REGIONS_FIELD]: [region('solo')] } as any, {}).map((r) => r.code)).toEqual([
      'solo'
    ]);
  });

  it('lists providers missing a region', () => {
    expect(
      providerUuidsNeedingRegion([provider('a'), provider('b')], { a: [region('r1')], b: [] })
    ).toEqual(['b']);
  });

  it('stampRegionProviderUuid copies region', () => {
    expect(stampRegionProviderUuid(region('r1'), 'p1').providerUuid).toBe('p1');
  });

  it('isMultiProviderPlacement includes Guided and Expert when flag on and count >= 2', () => {
    const multi = { providerConfigurations: [provider('a'), provider('b')] };
    expect(isMultiProviderPlacement(multi, ResilienceFormMode.GUIDED)).toBe(
      ENABLE_MULTI_PROVIDER_CREATE
    );
    expect(isMultiProviderPlacement(multi, ResilienceFormMode.EXPERT_MODE)).toBe(
      ENABLE_MULTI_PROVIDER_CREATE
    );
    expect(
      isMultiProviderPlacement({ providerConfigurations: [provider('a')] }, ResilienceFormMode.EXPERT_MODE)
    ).toBe(false);
  });
});
