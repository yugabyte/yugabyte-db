import { useTranslation } from 'react-i18next';
import { mui, yba, YBSmartStatus, YBTable, YBTooltip } from '@yugabyte-ui-library/core';

import { getSupportBundlePermissions } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { formatBytes } from '@app/utils/Formatters';
import {
  PaginationSpecDirection,
  SupportBundle,
  SupportBundleStatus
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import DeleteIcon from '@app/redesign/assets/approved/delete.svg';
import InfoIcon from '@app/redesign/assets/approved/info.svg';

import {
  getDaysUntilExpiry,
  getSupportBundleName,
  getSupportBundleStatusDisplay,
  isSupportBundleRunning
} from '../model/bundleDisplay';

const { styled, Box, IconButton } = mui;
const { YBButton } = yba;

const TRANSLATION_KEY_PREFIX = 'universeActions.supportBundleV2.list';
const PERMISSIONS = getSupportBundlePermissions(true);
const EMPTY_VALUE = '--';
/** The list API orders by creation date only, which is what bundle names encode. */
const NAME_COLUMN_ID = 'name';

interface BundleRow extends Record<string, unknown> {
  bundle: SupportBundle;
}
// material-react-table isn't installed in YBA, so YBTable's cell and sorting types are `any`.
interface CellProps {
  row: { original: BundleRow };
}
type SortingState = { id: string; desc: boolean }[];

const DeleteButton = styled(IconButton)(({ theme }) => ({
  padding: theme.spacing(0.5),
  borderRadius: 8,
  color: theme.palette.grey[700],
  '&.Mui-disabled': {
    color: theme.palette.grey[400]
  }
}));

/** The xlarge preset gives the design's 40px rows but a 42px header; the design's header is 34px. */
const TableFrame = styled(Box)({
  '& thead tr, & thead th': {
    height: 34
  }
});

interface SupportBundlesTableProps {
  bundles: SupportBundle[];
  universeUuid: string;
  now: Date;
  direction: PaginationSpecDirection;
  onDirectionChange: (direction: PaginationSpecDirection) => void;
  onShare: (bundle: SupportBundle) => void;
  onDelete: (bundle: SupportBundle) => void;
}

export const SupportBundlesTable = ({
  bundles,
  universeUuid,
  now,
  direction,
  onDirectionChange,
  onShare,
  onDelete
}: SupportBundlesTableProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const { t: tStatus } = useTranslation('translation', {
    keyPrefix: 'universeActions.supportBundleV2.status'
  });
  const sorting: SortingState = [
    { id: NAME_COLUMN_ID, desc: direction === PaginationSpecDirection.DESC }
  ];

  const columns = [
    {
      id: NAME_COLUMN_ID,
      size: 300,
      header: t('column.name'),
      accessorFn: ({ bundle }: BundleRow) => getSupportBundleName(bundle),
      Cell: ({ row: { original } }: CellProps) => (
        <span data-testid={`SupportBundleRow-${original.bundle.info?.uuid}`}>
          {getSupportBundleName(original.bundle)}
        </span>
      )
    },
    {
      id: 'redaction',
      size: 110,
      header: t('column.redaction'),
      enableSorting: false,
      Header: () => (
        <Box display="flex" alignItems="center" gap={0.5}>
          {t('column.redaction')}
          <YBTooltip title={t('redactionTooltip')}>
            <Box display="flex" color="grey.500">
              <InfoIcon width={14} height={14} />
            </Box>
          </YBTooltip>
        </Box>
      ),
      // Bundles don't report their redaction outcome yet.
      Cell: () => '-'
    },
    {
      id: 'size',
      size: 80,
      header: t('column.size'),
      enableSorting: false,
      Cell: ({ row: { original } }: CellProps) =>
        original.bundle.info?.status === SupportBundleStatus.Success &&
        original.bundle.info.size_in_bytes
          ? formatBytes(original.bundle.info.size_in_bytes)
          : EMPTY_VALUE
    },
    {
      id: 'expires',
      size: 100,
      header: t('column.expires'),
      enableSorting: false,
      Cell: ({ row: { original } }: CellProps) => {
        const days = getDaysUntilExpiry(original.bundle.info?.expiration_date, now);
        return days === null ? EMPTY_VALUE : t('expiresInDays', { count: days });
      }
    },
    {
      id: 'status',
      size: 160,
      header: t('column.status'),
      enableSorting: false,
      Cell: ({ row: { original } }: CellProps) => {
        const display = getSupportBundleStatusDisplay(original.bundle.info?.status);
        return <YBSmartStatus type={display.statusType} label={tStatus(display.i18nKey)} />;
      }
    },
    {
      id: 'actions',
      size: 120,
      header: '',
      enableSorting: false,
      Cell: ({ row: { original } }: CellProps) => {
        const { bundle } = original;
        const bundleUuid = bundle.info?.uuid ?? '';
        const isRunning = isSupportBundleRunning(bundle);
        return (
          <Box display="flex" alignItems="center" justifyContent="flex-end" gap={1.5}>
            <RbacValidator
              isControl
              accessRequiredOn={{ ...PERMISSIONS.download, onResource: { UNIVERSE: universeUuid } }}
              popOverOverrides={{ zIndex: 100000 }}
            >
              <YBButton
                variant="secondary"
                size="medium"
                disabled={bundle.info?.status !== SupportBundleStatus.Success}
                onClick={() => onShare(bundle)}
                dataTestId={`SupportBundleRow-Share-${bundleUuid}`}
              >
                {t('share')}
              </YBButton>
            </RbacValidator>
            <RbacValidator
              isControl
              accessRequiredOn={{ ...PERMISSIONS.delete, onResource: { UNIVERSE: universeUuid } }}
              popOverOverrides={{ zIndex: 100000 }}
            >
              <YBTooltip title={isRunning ? t('deleteDisabledRunning') : ''}>
                {/* Disabled buttons don't fire mouse events, so the tooltip needs a wrapper. */}
                <span>
                  <DeleteButton
                    aria-label={t('delete')}
                    disabled={isRunning}
                    onClick={() => onDelete(bundle)}
                    data-testid={`SupportBundleRow-Delete-${bundleUuid}`}
                  >
                    <DeleteIcon />
                  </DeleteButton>
                </span>
              </YBTooltip>
            </RbacValidator>
          </Box>
        );
      }
    }
  ];

  return (
    <TableFrame>
      <YBTable
        size="xlarge"
        dataTestId="SupportBundlesTable"
        data={bundles.map((bundle): BundleRow => ({ bundle }))}
        columns={columns}
        options={{
          pagination: false,
          manualSorting: true,
          enableSortingRemoval: false,
          state: { sorting },
          onSortingChange: (updater: SortingState | ((old: SortingState) => SortingState)) => {
            const [next] = typeof updater === 'function' ? updater(sorting) : updater;
            onDirectionChange(
              next?.desc === false ? PaginationSpecDirection.ASC : PaginationSpecDirection.DESC
            );
          }
        }}
      />
    </TableFrame>
  );
};
