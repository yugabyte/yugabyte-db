import { describe, expect, it } from 'vitest';

import { SUPPORT_BUNDLE_REDESIGN_ENABLED_KEY, SUPPORT_BUNDLE_UI_V2_ENABLED_KEY } from './constants';
import { isSupportBundleRedesignEnabled } from './isSupportBundleRedesignEnabled';

const entries = (v2Api: boolean, redesign: boolean) => [
  { key: SUPPORT_BUNDLE_UI_V2_ENABLED_KEY, value: String(v2Api) },
  { key: SUPPORT_BUNDLE_REDESIGN_ENABLED_KEY, value: String(redesign) }
];

describe('isSupportBundleRedesignEnabled', () => {
  it('needs both the v2 API key and the redesign flag', () => {
    expect(isSupportBundleRedesignEnabled(entries(true, true))).toBe(true);
    expect(isSupportBundleRedesignEnabled(entries(false, true))).toBe(false);
    expect(isSupportBundleRedesignEnabled(entries(true, false))).toBe(false);
  });
});
