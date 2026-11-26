import { SUPPORT_BUNDLE_UI_V2_ENABLED_KEY } from './constants';

export interface RuntimeConfigEntry {
  key?: string;
  value?: string;
}

export const isSupportBundleUiV2Enabled = (
  configEntries: RuntimeConfigEntry[] | undefined | null
): boolean =>
  configEntries?.find((entry) => entry.key === SUPPORT_BUNDLE_UI_V2_ENABLED_KEY)?.value === 'true';
