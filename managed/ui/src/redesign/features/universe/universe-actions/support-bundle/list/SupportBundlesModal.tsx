import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, yba, YBPagination } from '@yugabyte-ui-library/core';

import { getSupportBundlePermissions } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import {
  PaginationSpecDirection,
  SupportBundle
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import IdeaIcon from '@app/redesign/assets/approved/idea.svg';

import { useSupportBundleList } from '../hooks/useSupportBundles';
import { SUPPORT_BUNDLE_PAGE_SIZE } from '../supportBundleApi';
import { DeleteSupportBundleModal } from './DeleteSupportBundleModal';
import { SupportBundlesEmptyState } from './SupportBundlesEmptyState';
import { SupportBundlesTable } from './SupportBundlesTable';

const { Box, Typography, CircularProgress } = mui;
const { YBModal, YBButton } = yba;

const TRANSLATION_KEY_PREFIX = 'universeActions.supportBundleV2';
const PERMISSIONS = getSupportBundlePermissions(true);

interface SupportBundlesModalProps {
  universeUuid: string;
  onClose: () => void;
  /** Opens the create flow; Create is disabled until a handler is given. */
  onCreateBundle?: () => void;
  onShareBundle: (bundle: SupportBundle) => void;
}

export const SupportBundlesModal = ({
  universeUuid,
  onClose,
  onCreateBundle,
  onShareBundle
}: SupportBundlesModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const { t: tCommon } = useTranslation('translation', { keyPrefix: 'common' });
  // Zero-based; YBPagination is one-based.
  const [page, setPage] = useState(0);
  const [direction, setDirection] = useState<PaginationSpecDirection>(PaginationSpecDirection.DESC);
  const [bundleToDelete, setBundleToDelete] = useState<SupportBundle | null>(null);
  const listQuery = useSupportBundleList(universeUuid, page, direction);

  const bundles = listQuery.data?.entities ?? [];
  const totalCount = listQuery.data?.total_count ?? 0;
  const pageCount = Math.ceil(totalCount / SUPPORT_BUNDLE_PAGE_SIZE);
  // Deletes and expiry shrink the list under the user, possibly past the page they were on.
  const lastPage = Math.max(0, pageCount - 1);
  useEffect(() => {
    if (listQuery.data && page > lastPage) {
      setPage(lastPage);
    }
  }, [listQuery.data, page, lastPage]);
  const now = new Date();

  const renderBody = () => {
    if (listQuery.isLoading) {
      return (
        <Box display="flex" justifyContent="center" padding={6}>
          <CircularProgress size={32} />
        </Box>
      );
    }
    if (listQuery.isError && !listQuery.data) {
      return (
        <Typography variant="body2" color="error" padding={3} textAlign="center">
          {t('list.loadFailed')}
        </Typography>
      );
    }
    if (totalCount === 0) {
      return (
        <SupportBundlesEmptyState universeUuid={universeUuid} onCreateBundle={onCreateBundle} />
      );
    }
    return (
      <>
        <Box
          display="flex"
          alignItems="center"
          justifyContent="space-between"
          padding="16px 16px 0"
        >
          <Typography variant="body1" fontSize={14} color="text.secondary">
            {t('list.bundleCount', { count: totalCount })}
          </Typography>
          <RbacValidator
            isControl
            accessRequiredOn={{ ...PERMISSIONS.create, onResource: { UNIVERSE: universeUuid } }}
            popOverOverrides={{ zIndex: 100000 }}
          >
            <YBButton
              variant="ybaPrimary"
              size="large"
              disabled={!onCreateBundle}
              onClick={onCreateBundle}
              dataTestId="SupportBundlesModal-Create"
            >
              {t('list.createBundle')}
            </YBButton>
          </RbacValidator>
        </Box>
        <Box padding="16px 16px 24px">
          <SupportBundlesTable
            bundles={bundles}
            universeUuid={universeUuid}
            now={now}
            direction={direction}
            onDirectionChange={(next) => {
              setDirection(next);
              setPage(0);
            }}
            onShare={onShareBundle}
            onDelete={setBundleToDelete}
          />
          {pageCount > 1 && (
            <Box display="flex" justifyContent="flex-end" marginTop={2}>
              <YBPagination
                pageSize={pageCount}
                currentPage={page + 1}
                onPageSelect={(selectedPage) => setPage(selectedPage - 1)}
                dataTestId="SupportBundlesModal-Pagination"
              />
            </Box>
          )}
          <Box display="flex" alignItems="center" gap={0.5} marginTop={2} color="grey.600">
            <IdeaIcon width={20} height={20} />
            <Typography variant="subtitle1" color="grey.700">
              {t('list.expiryNote')}
            </Typography>
          </Box>
        </Box>
      </>
    );
  };

  return (
    <>
      <YBModal
        open
        onClose={onClose}
        title={t('list.title')}
        cancelLabel={tCommon('close')}
        cancelTestId="SupportBundlesModal-Close"
        size="xl"
        overrideWidth={944}
        overrideHeight="fit-content"
        titleSeparator
        dialogContentProps={{ sx: { padding: '0 !important' } }}
      >
        {renderBody()}
      </YBModal>
      <DeleteSupportBundleModal
        universeUuid={universeUuid}
        bundle={bundleToDelete}
        onClose={() => setBundleToDelete(null)}
      />
    </>
  );
};
