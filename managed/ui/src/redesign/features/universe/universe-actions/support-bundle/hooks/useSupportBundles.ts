import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDebounce } from 'react-use';
import { useState } from 'react';

import { supportBundleQueryKey } from '@app/redesign/helpers/api';
import {
  createSupportBundle,
  deleteSupportBundle,
  estimateSupportBundleSize,
  pageListSupportBundles
} from '@app/v2/api/support-bundle/support-bundle';
import {
  PaginationSpecDirection,
  SupportBundleCreateSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { isSupportBundleRunning } from '../model/bundleDisplay';
import { SUPPORT_BUNDLE_PAGE_SIZE } from '../supportBundleApi';

export const RUNNING_BUNDLE_POLL_INTERVAL_MS = 10_000;
const ESTIMATE_DEBOUNCE_MS = 500;

/**
 * One page of bundles, ordered by creation date (newest first by default). Polls while any bundle
 * on the page is still running.
 */
export const useSupportBundleList = (
  universeUuid: string,
  page: number,
  direction: PaginationSpecDirection = PaginationSpecDirection.DESC
) =>
  useQuery(
    [...supportBundleQueryKey.list(universeUuid, true, page), direction],
    () =>
      pageListSupportBundles(universeUuid, {
        limit: SUPPORT_BUNDLE_PAGE_SIZE,
        offset: page * SUPPORT_BUNDLE_PAGE_SIZE,
        direction
      }),
    {
      keepPreviousData: true,
      refetchInterval: (data) =>
        data?.entities?.some(isSupportBundleRunning) ? RUNNING_BUNDLE_POLL_INTERVAL_MS : false
    }
  );

/**
 * Mutations invalidate only the lists: the estimate key also sits under `supportBundleQueryKey.ALL`,
 * and refetching it would size files on every node again.
 */
const listQueryPrefix = (universeUuid: string) => [
  ...supportBundleQueryKey.ALL,
  'list',
  universeUuid
];

export const useCreateSupportBundleV2 = (universeUuid: string) => {
  const queryClient = useQueryClient();
  return useMutation((spec: SupportBundleCreateSpec) => createSupportBundle(universeUuid, spec), {
    onSuccess: () => queryClient.invalidateQueries(listQueryPrefix(universeUuid))
  });
};

export const useDeleteSupportBundleV2 = (universeUuid: string) => {
  const queryClient = useQueryClient();
  return useMutation((bundleUuid: string) => deleteSupportBundle(universeUuid, bundleUuid), {
    onSuccess: () => queryClient.invalidateQueries(listQueryPrefix(universeUuid))
  });
};

/**
 * Size estimate for the spec, debounced so form edits don't each start a request.
 *
 * The spec is part of the query key, so callers must build it from a `now` they hold steady
 * (e.g. reset on "Refresh estimate"); a spec rebuilt from `new Date()` on every render would
 * never settle.
 */
export const useSupportBundleEstimate = (
  universeUuid: string,
  spec: SupportBundleCreateSpec | null
) => {
  const [debouncedSpec, setDebouncedSpec] = useState(spec);
  useDebounce(() => setDebouncedSpec(spec), ESTIMATE_DEBOUNCE_MS, [JSON.stringify(spec)]);

  const isEnabled = !!debouncedSpec && debouncedSpec.components.length > 0;
  const query = useQuery(
    supportBundleQueryKey.estimate(universeUuid, debouncedSpec),
    () => estimateSupportBundleSize(universeUuid, debouncedSpec as SupportBundleCreateSpec),
    { enabled: isEnabled, staleTime: Infinity, cacheTime: 0, refetchOnWindowFocus: false }
  );
  const isDebouncing = JSON.stringify(spec) !== JSON.stringify(debouncedSpec);
  return {
    ...query,
    isEstimating: isEnabled && (isDebouncing || query.isFetching),
    asOf: query.dataUpdatedAt ? new Date(query.dataUpdatedAt) : null
  };
};
