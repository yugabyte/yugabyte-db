export const OciImageUpgradeMode = {
  IN_PLACE: 'in-place',
  MARKETPLACE_BASED: 'marketplace-based',
  UNKNOWN: 'unknown'
} as const;
export type OciImageUpgradeMode = (typeof OciImageUpgradeMode)[keyof typeof OciImageUpgradeMode];

export interface OciImageBundleRegions {
  details?: {
    regions?: Record<string, { isImageMarketplaceBased?: boolean | null } | undefined>;
  };
}

/** A bundle checked only in the regions where its nodes run. */
export interface OciBundleRegionCheck {
  bundle: OciImageBundleRegions | undefined;
  regionCodes: string[];
}

/**
 * Each bundle is checked only in the regions where its nodes run.
 * A true flag on any checked pair results in early `OciImageUpgradeMode.MARKETPLACE_BASED`
 * return because that image cannot take the default in-place path.
 * A missing flag means the upgrade task looks the image up itself.
 * A check with no bundle is skipped.
 */
export const getOciImageBundleUpgradeMode = (
  checks: OciBundleRegionCheck[]
): OciImageUpgradeMode => {
  let hasMissingFlag = false;

  for (const check of checks) {
    if (!check.bundle) {
      continue; // Skip checks with no bundle
    }
    const regionCodes = [
      ...new Set(check.regionCodes.filter((regionCode) => regionCode.length > 0))
    ];
    for (const regionCode of regionCodes) {
      const isImageMarketplaceBased =
        check.bundle.details?.regions?.[regionCode]?.isImageMarketplaceBased;
      if (isImageMarketplaceBased === true) {
        return OciImageUpgradeMode.MARKETPLACE_BASED;
      }
      if (isImageMarketplaceBased === null || isImageMarketplaceBased === undefined) {
        hasMissingFlag = true;
      }
    }
  }

  if (hasMissingFlag) {
    return OciImageUpgradeMode.UNKNOWN;
  }
  return OciImageUpgradeMode.IN_PLACE;
};
