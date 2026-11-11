import { describe, expect, it } from 'vitest';

import {
  OciImageUpgradeMode,
  getOciImageBundleUpgradeMode,
  type OciBundleRegionCheck,
  type OciImageBundleRegions
} from './ociImageBundleUpgrade';

const bundle = (
  regions: Record<string, { isImageMarketplaceBased?: boolean | null }>
): OciImageBundleRegions => ({
  details: { regions }
});

const check = (
  regions: Record<string, { isImageMarketplaceBased?: boolean | null }>,
  regionCodes: string[]
): OciBundleRegionCheck => ({
  bundle: bundle(regions),
  regionCodes
});

describe('getOciImageBundleUpgradeMode', () => {
  it('uses in-place when every checked region is not marketplace-based', () => {
    expect(
      getOciImageBundleUpgradeMode([check({ 'us-ashburn-1': { isImageMarketplaceBased: false } }, [
        'us-ashburn-1'
      ])])
    ).toBe(OciImageUpgradeMode.IN_PLACE);
  });

  it('uses marketplace-based when the current or target bundle has a true flag', () => {
    const current = check({ 'us-ashburn-1': { isImageMarketplaceBased: false } }, ['us-ashburn-1']);
    const target = check({ 'us-ashburn-1': { isImageMarketplaceBased: true } }, ['us-ashburn-1']);

    expect(getOciImageBundleUpgradeMode([current])).toBe(OciImageUpgradeMode.IN_PLACE);
    expect(getOciImageBundleUpgradeMode([current, target])).toBe(
      OciImageUpgradeMode.MARKETPLACE_BASED
    );
    expect(
      getOciImageBundleUpgradeMode([
        check({ 'us-ashburn-1': { isImageMarketplaceBased: true } }, ['us-ashburn-1'])
      ])
    ).toBe(OciImageUpgradeMode.MARKETPLACE_BASED);
  });

  it('uses unknown when a checked region has no flag and none are marketplace-based', () => {
    expect(
      getOciImageBundleUpgradeMode([
        check(
          {
            'us-ashburn-1': { isImageMarketplaceBased: false },
            'us-phoenix-1': {}
          },
          ['us-ashburn-1', 'us-phoenix-1']
        )
      ])
    ).toBe(OciImageUpgradeMode.UNKNOWN);

    expect(
      getOciImageBundleUpgradeMode([
        check({ 'us-ashburn-1': { isImageMarketplaceBased: null } }, ['us-ashburn-1'])
      ])
    ).toBe(OciImageUpgradeMode.UNKNOWN);

    expect(
      getOciImageBundleUpgradeMode([
        check({ 'us-phoenix-1': { isImageMarketplaceBased: false } }, ['us-ashburn-1'])
      ])
    ).toBe(OciImageUpgradeMode.UNKNOWN);
  });

  it('prefers marketplace-based over a missing flag', () => {
    expect(
      getOciImageBundleUpgradeMode([
        check({ 'us-ashburn-1': {} }, ['us-ashburn-1']),
        check({ 'us-phoenix-1': { isImageMarketplaceBased: true } }, ['us-phoenix-1'])
      ])
    ).toBe(OciImageUpgradeMode.MARKETPLACE_BASED);
  });

  it('ignores regions the cluster does not use', () => {
    expect(
      getOciImageBundleUpgradeMode([
        check(
          {
            'us-ashburn-1': { isImageMarketplaceBased: false },
            'us-phoenix-1': { isImageMarketplaceBased: true }
          },
          ['us-ashburn-1']
        )
      ])
    ).toBe(OciImageUpgradeMode.IN_PLACE);
  });

  it('ignores a missing flag on a replica bundle outside the replica regions', () => {
    expect(
      getOciImageBundleUpgradeMode([
        check({ 'us-ashburn-1': { isImageMarketplaceBased: false } }, ['us-ashburn-1']),
        check(
          {
            'us-ashburn-1': { isImageMarketplaceBased: null },
            'us-phoenix-1': { isImageMarketplaceBased: false }
          },
          ['us-phoenix-1']
        )
      ])
    ).toBe(OciImageUpgradeMode.IN_PLACE);
  });

  it('ignores a true flag on the primary bundle outside the primary regions', () => {
    expect(
      getOciImageBundleUpgradeMode([
        check(
          {
            'us-ashburn-1': { isImageMarketplaceBased: false },
            'us-phoenix-1': { isImageMarketplaceBased: true }
          },
          ['us-ashburn-1']
        ),
        check({ 'us-phoenix-1': { isImageMarketplaceBased: false } }, ['us-phoenix-1'])
      ])
    ).toBe(OciImageUpgradeMode.IN_PLACE);
  });

  it('uses marketplace-based when a replica bundle is marketplace-based in its own region', () => {
    expect(
      getOciImageBundleUpgradeMode([
        check({ 'us-ashburn-1': { isImageMarketplaceBased: false } }, ['us-ashburn-1']),
        check({ 'us-phoenix-1': { isImageMarketplaceBased: true } }, ['us-phoenix-1'])
      ])
    ).toBe(OciImageUpgradeMode.MARKETPLACE_BASED);
  });

  it('uses marketplace-based when the target is marketplace-based in a replica-only region', () => {
    expect(
      getOciImageBundleUpgradeMode([
        check({ 'us-ashburn-1': { isImageMarketplaceBased: false } }, ['us-ashburn-1']),
        check({ 'us-phoenix-1': { isImageMarketplaceBased: false } }, ['us-phoenix-1']),
        check(
          {
            'us-ashburn-1': { isImageMarketplaceBased: false },
            'us-phoenix-1': { isImageMarketplaceBased: true }
          },
          ['us-ashburn-1', 'us-phoenix-1']
        )
      ])
    ).toBe(OciImageUpgradeMode.MARKETPLACE_BASED);
  });
});
