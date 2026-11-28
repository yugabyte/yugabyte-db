import { useEffect, useRef, useState } from 'react';
import { AxiosError } from 'axios';
import { useQueries } from 'react-query';
import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { mui, yba } from '@yugabyte-ui-library/core';

import { getSupportBundlePermissions } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { supportBundleQueryKey } from '@app/redesign/helpers/api';
import { formatBytes } from '@app/utils/Formatters';
import { getSupportBundle } from '@app/v2/api/support-bundle/support-bundle';
import { SupportBundle, SupportBundleStatus } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import CheckIcon from '@app/redesign/assets/approved/check.svg';
import CloseLargeIcon from '@app/redesign/assets/approved/close-large.svg';

import { RUNNING_BUNDLE_POLL_INTERVAL_MS } from '../hooks/useSupportBundles';
import { getSupportBundleName, isSupportBundleRunning } from '../model/bundleDisplay';
import { ShareBundleModal } from '../share/ShareBundleModal';
import {
  forgetSupportBundle,
  markSupportBundleReady,
  useTrackedSupportBundles
} from './pendingSupportBundles';

const { styled, Box, Typography, IconButton, Snackbar } = mui;
const { YBButton } = yba;

const PERMISSIONS = getSupportBundlePermissions(true);
/** Status checks that fail this many times in a row stop being retried. */
const MAX_FAILED_STATUS_CHECKS = 3;

const isNotFound = (error: unknown) => (error as AxiosError)?.response?.status === 404;

const Card = styled(Box)(({ theme }) => ({
  display: 'flex',
  alignItems: 'flex-start',
  gap: theme.spacing(2),
  width: 487,
  padding: theme.spacing(2, 2, 2.5),
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: 12,
  background: theme.palette.common.white,
  boxShadow: '0px 3px 15px rgba(102, 102, 102, 0.15)'
}));

const CheckBadge = styled(Box)(({ theme }) => ({
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  flexShrink: 0,
  width: 32,
  height: 32,
  borderRadius: '50%',
  background: theme.palette.success[100],
  color: theme.palette.success[500]
}));

interface SupportBundleReadyWatcherProps {
  universeUuid: string;
  /** Shown when a bundle was collected from every node. */
  universeNodeCount: number;
}

/**
 * Follows bundles created in this session and announces each one once it is ready, wherever the
 * user is on the universe page.
 */
export const SupportBundleReadyWatcher = ({
  universeUuid,
  universeNodeCount
}: SupportBundleReadyWatcherProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.supportBundleV2.ready'
  });
  const { pending, ready } = useTrackedSupportBundles(universeUuid);
  const [bundleToShare, setBundleToShare] = useState<SupportBundle | null>(null);

  const queries = useQueries(
    pending.map((bundleUuid) => ({
      queryKey: supportBundleQueryKey.detail(universeUuid, bundleUuid),
      queryFn: () => getSupportBundle(universeUuid, bundleUuid),
      refetchInterval: RUNNING_BUNDLE_POLL_INTERVAL_MS,
      retry: (failureCount: number, error: unknown) => !isNotFound(error) && failureCount < 3
    }))
  );

  // Consecutive failed status checks per bundle, keyed by the failure's timestamp so a re-render
  // doesn't count the same failure twice.
  const failedChecks = useRef(new Map<string, { count: number; lastAt: number }>());

  useEffect(() => {
    queries.forEach((query, index) => {
      const bundleUuid = pending[index];
      const failures = failedChecks.current.get(bundleUuid) ?? { count: 0, lastAt: 0 };
      if (query.isError && query.errorUpdatedAt !== failures.lastAt) {
        failures.count += 1;
        failures.lastAt = query.errorUpdatedAt;
        failedChecks.current.set(bundleUuid, failures);
      } else if (query.isSuccess) {
        failedChecks.current.delete(bundleUuid);
      }
      if (query.isError) {
        // Gone (deleted or expired elsewhere), or the status can't be read: stop polling for it.
        if (isNotFound(query.error)) {
          forgetSupportBundle(universeUuid, bundleUuid);
        } else if (failures.count >= MAX_FAILED_STATUS_CHECKS) {
          forgetSupportBundle(universeUuid, bundleUuid);
          toast.error(t('statusUnavailable'));
        }
        return;
      }
      const bundle = query.data;
      if (!bundle || isSupportBundleRunning(bundle)) {
        return;
      }
      if (bundle.info?.status === SupportBundleStatus.Success) {
        markSupportBundleReady(universeUuid, bundleUuid, bundle);
      } else {
        forgetSupportBundle(universeUuid, bundleUuid);
        toast.error(t('failed', { bundleName: getSupportBundleName(bundle) }));
      }
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [queries.map((query) => `${query.data?.info?.status}:${query.errorUpdatedAt}`).join()]);

  const dismiss = (bundle: SupportBundle) =>
    forgetSupportBundle(universeUuid, bundle.info?.uuid ?? '');
  const bundle = ready[0];
  const nodeCount = bundle?.spec?.node_names?.length || universeNodeCount;

  return (
    <>
      <Snackbar open={!!bundle} anchorOrigin={{ vertical: 'bottom', horizontal: 'right' }}>
        <Card data-testid="SupportBundleReadyBanner">
          <CheckBadge>
            <CheckIcon />
          </CheckBadge>
          <Box flex={1} display="flex" flexDirection="column" gap={1.5}>
            <Box display="flex" flexDirection="column" gap={1} paddingY={1}>
              <Typography variant="h5" color="text.primary">
                {t('title')}
              </Typography>
              <Typography variant="body2" color="grey.700">
                {t('body', {
                  size: formatBytes(bundle?.info?.size_in_bytes ?? 0),
                  count: nodeCount
                })}
              </Typography>
            </Box>
            <Box display="flex" gap={1}>
              <RbacValidator
                isControl
                accessRequiredOn={{
                  ...PERMISSIONS.download,
                  onResource: { UNIVERSE: universeUuid }
                }}
                popOverOverrides={{ zIndex: 100000 }}
              >
                <YBButton
                  variant="primary"
                  onClick={() => {
                    setBundleToShare(bundle);
                    dismiss(bundle);
                  }}
                  dataTestId="SupportBundleReadyBanner-Share"
                >
                  {t('share')}
                </YBButton>
              </RbacValidator>
              <YBButton
                variant="secondary"
                onClick={() => dismiss(bundle)}
                dataTestId="SupportBundleReadyBanner-Dismiss"
              >
                {t('dismiss')}
              </YBButton>
            </Box>
          </Box>
          <IconButton
            aria-label={t('dismiss')}
            onClick={() => dismiss(bundle)}
            data-testid="SupportBundleReadyBanner-Close"
          >
            <CloseLargeIcon />
          </IconButton>
        </Card>
      </Snackbar>
      <ShareBundleModal
        // A new bundle needs its own acknowledgement.
        key={bundleToShare?.info?.uuid}
        universeUuid={universeUuid}
        bundle={bundleToShare}
        onClose={() => setBundleToShare(null)}
      />
    </>
  );
};
