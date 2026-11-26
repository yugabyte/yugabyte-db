import axios from 'axios';

import { ROOT_URL } from '@app/config';
import {
  createSupportBundle as createSupportBundleV2,
  deleteSupportBundle as deleteSupportBundleV2,
  estimateSupportBundleSize as estimateSupportBundleSizeV2,
  pageListSupportBundles
} from '@app/v2/api/support-bundle/support-bundle';
import {
  PaginationSpecDirection,
  type SupportBundleSizeEstimateResponse
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { mapSupportBundlePayloadToV2 } from './mapSupportBundlePayload';
import { mapSupportBundleListToUi } from './mapSupportBundleResponse';
import type { UiSupportBundle, UiSupportBundleCreatePayload } from './supportBundleTypes';

export const SUPPORT_BUNDLE_PAGE_SIZE = 25;

export interface ListSupportBundlesResult {
  bundles: UiSupportBundle[];
  totalCount: number;
}

const v1BasePath = (universeUUID: string) => {
  const customerUUID = localStorage.getItem('customerId');
  return `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle`;
};

const v2DownloadPath = (universeUUID: string, bundleUUID: string) => {
  const customerUUID = localStorage.getItem('customerId');
  const v2RootUrl = ROOT_URL.replace('/api/v1', '/api/v2');
  return `${v2RootUrl}/customers/${customerUUID}/universes/${universeUUID}/support-bundles/${bundleUUID}/download`;
};

/**
 * List support bundles for a universe.
 * v1: GET support_bundle (full list).
 * v2: POST support-bundles/page (paged; sorted DESC by creation date).
 */
export const listSupportBundles = async (
  universeUUID: string,
  useV2Api: boolean,
  offset = 0
): Promise<ListSupportBundlesResult> => {
  if (!useV2Api) {
    const response = await axios.get(v1BasePath(universeUUID));
    const bundles = response.data as UiSupportBundle[];
    return { bundles, totalCount: bundles.length };
  }

  const pagedResponse = await pageListSupportBundles(universeUUID, {
    limit: SUPPORT_BUNDLE_PAGE_SIZE,
    offset,
    direction: PaginationSpecDirection.DESC
  });
  const totalCount = pagedResponse.total_count ?? 0;
  return {
    bundles: mapSupportBundleListToUi(pagedResponse.entities),
    totalCount
  };
};

/**
 * Create a support bundle.
 * Returns the raw API result (v1 axios data or v2 YBATask).
 */
export const createSupportBundle = async (
  universeUUID: string,
  payload: UiSupportBundleCreatePayload,
  useV2Api: boolean
) => {
  if (!useV2Api) {
    const response = await axios.post(v1BasePath(universeUUID), payload);
    return response.data;
  }
  return createSupportBundleV2(universeUUID, mapSupportBundlePayloadToV2(payload));
};

/**
 * Delete a support bundle by UUID.
 */
export const deleteSupportBundle = async (
  universeUUID: string,
  bundleUUID: string,
  useV2Api: boolean
) => {
  if (!useV2Api) {
    const response = await axios.delete(`${v1BasePath(universeUUID)}/${bundleUUID}`);
    return response.data;
  }
  return deleteSupportBundleV2(universeUUID, bundleUUID);
};

/**
 * Download a completed support bundle archive.
 * Opens the download URL in a new tab (cookie auth) so the browser can
 * stream the archive and honor Content-Disposition.
 */
export const downloadSupportBundle = (
  universeUUID: string,
  bundleUUID: string,
  useV2Api: boolean
) => {
  const downloadUrl = useV2Api
    ? v2DownloadPath(universeUUID, bundleUUID)
    : `${v1BasePath(universeUUID)}/${bundleUUID}/download`;
  window.open(downloadUrl, '_blank');
};

/**
 * Estimate support bundle size for the given payload.
 * Response shape (`{ data: { [node]: { [component]: bytes } } }`) is the same on v1 and v2.
 */
export const estimateSupportBundleSize = async (
  universeUUID: string,
  payload: UiSupportBundleCreatePayload,
  useV2Api: boolean
): Promise<SupportBundleSizeEstimateResponse> => {
  if (!useV2Api) {
    const response = await axios.post(`${v1BasePath(universeUUID)}/estimate_size`, payload);
    return response.data as SupportBundleSizeEstimateResponse;
  }
  return estimateSupportBundleSizeV2(universeUUID, mapSupportBundlePayloadToV2(payload));
};
