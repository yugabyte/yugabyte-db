import type { SupportBundle } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import type { UiSupportBundle } from './supportBundleTypes';

/**
 * Maps a generated v2 SupportBundle (spec + info) onto the flat UiSupportBundle
 * shape consumed by ThirdStep.
 */
export const mapSupportBundleResponseToUi = (bundle: SupportBundle): UiSupportBundle => ({
  bundleUUID: bundle.info?.uuid ?? '',
  creationDate: bundle.info?.creation_date ?? null,
  expirationDate: bundle.info?.expiration_date ?? null,
  status: bundle.info?.status ?? 'Running',
  sizeInBytes: bundle.info?.size_in_bytes ?? 0
});

export const mapSupportBundleListToUi = (bundles: SupportBundle[] | undefined): UiSupportBundle[] =>
  (bundles ?? []).map(mapSupportBundleResponseToUi);
