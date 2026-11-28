import { SUPPORT_BUNDLE_REDESIGN_ENABLED_KEY } from './constants';
import { isSupportBundleUiV2Enabled, RuntimeConfigEntry } from './isSupportBundleUiV2Enabled';

/**
 * The redesigned UI talks only to the v2 API, so it needs the v2 API key as well as its own
 * temporary flag. When the redesign ships, the temporary flag goes away and the v2 API key alone
 * turns it on.
 */
export const isSupportBundleRedesignEnabled = (
  configEntries: RuntimeConfigEntry[] | undefined | null
): boolean =>
  isSupportBundleUiV2Enabled(configEntries) &&
  configEntries?.find((entry) => entry.key === SUPPORT_BUNDLE_REDESIGN_ENABLED_KEY)?.value ===
    'true';
