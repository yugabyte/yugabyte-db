import { useEffect, useState } from 'react';
import { useQueries } from 'react-query';
import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { mui, yba } from '@yugabyte-ui-library/core';

import { supportBundleQueryKey } from '@app/redesign/helpers/api';
import { formatBytes } from '@app/utils/Formatters';
import { getSupportBundle } from '@app/v2/api/support-bundle/support-bundle';
import { SupportBundle, SupportBundleStatus } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import CheckIcon from '@app/redesign/assets/approved/check.svg';
import CloseLargeIcon from '@app/redesign/assets/approved/close-large.svg';

import { getSupportBundleName } from '../model/bundleDisplay';
import { ShareBundleModal } from '../share/ShareBundleModal';
import { removePendingSupportBundle, usePendingSupportBundles } from './pendingSupportBundles';

const { styled, Box, Typography, IconButton, Snackbar } = mui;
const { YBButton } = yba;

const POLL_INTERVAL_MS = 10_000;

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
  const pending = usePendingSupportBundles(universeUuid);
  const [readyBundles, setReadyBundles] = useState<SupportBundle[]>([]);
  const [bundleToShare, setBundleToShare] = useState<SupportBundle | null>(null);

  const queries = useQueries(
    pending.map((bundleUuid) => ({
      queryKey: [...supportBundleQueryKey.ALL, 'detail', universeUuid, bundleUuid],
      queryFn: () => getSupportBundle(universeUuid, bundleUuid),
      refetchInterval: POLL_INTERVAL_MS
    }))
  );

  useEffect(() => {
    queries.forEach(({ data: bundle }) => {
      const bundleUuid = bundle?.info?.uuid;
      const status = bundle?.info?.status;
      if (!bundle || !bundleUuid || status === SupportBundleStatus.Running) {
        return;
      }
      removePendingSupportBundle(universeUuid, bundleUuid);
      if (status === SupportBundleStatus.Success) {
        setReadyBundles((current) => [...current, bundle]);
      } else {
        toast.error(t('failed', { bundleName: getSupportBundleName(bundle) }));
      }
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [queries.map((query) => query.data?.info?.status).join()]);

  const dismiss = (bundle: SupportBundle) =>
    setReadyBundles((current) => current.filter((ready) => ready !== bundle));
  const bundle = readyBundles[0];
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
        universeUuid={universeUuid}
        bundle={bundleToShare}
        onClose={() => setBundleToShare(null)}
      />
    </>
  );
};
