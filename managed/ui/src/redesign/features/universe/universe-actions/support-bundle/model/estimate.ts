import type { SupportBundleSizeEstimateResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { getCategoryForComponent, SupportBundleCategoryId } from './categories';

/** Matches `formatBytes`, which uses decimal units, so "20 GB" in the UI means this many bytes. */
export const LARGE_BUNDLE_THRESHOLD_BYTES = 20 * 1000 * 1000 * 1000;

export interface SupportBundleEstimateSummary {
  totalBytes: number;
  componentBytes: Record<string, number>;
  categoryBytes: Partial<Record<SupportBundleCategoryId, number>>;
  isLarge: boolean;
}

/** Sums the per-node estimate (global components arrive under the `YBA` key). */
export const summarizeEstimate = (
  response: SupportBundleSizeEstimateResponse | undefined
): SupportBundleEstimateSummary => {
  const componentBytes: Record<string, number> = {};
  Object.values(response?.data ?? {}).forEach((nodeComponents) => {
    Object.entries(nodeComponents).forEach(([component, bytes]) => {
      componentBytes[component] = (componentBytes[component] ?? 0) + bytes;
    });
  });

  const categoryBytes: Partial<Record<SupportBundleCategoryId, number>> = {};
  let totalBytes = 0;
  Object.entries(componentBytes).forEach(([component, bytes]) => {
    totalBytes += bytes;
    const categoryId = getCategoryForComponent(component);
    if (categoryId) {
      categoryBytes[categoryId] = (categoryBytes[categoryId] ?? 0) + bytes;
    }
  });

  return {
    totalBytes,
    componentBytes,
    categoryBytes,
    isLarge: totalBytes > LARGE_BUNDLE_THRESHOLD_BYTES
  };
};
