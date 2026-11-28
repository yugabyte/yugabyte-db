import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { AlertVariant, mui, yba, YBAlert } from '@yugabyte-ui-library/core';

import { YBTimeFormats, ybFormatDate } from '@app/redesign/helpers/DateUtils';
import { formatBytes } from '@app/utils/Formatters';
import RefreshIcon from '@app/redesign/assets/approved/refresh.svg';

import { SupportBundleCategoryDef } from '../model/categories';
import { SupportBundleEstimateSummary } from '../model/estimate';

const { styled, Box, Typography, ButtonBase } = mui;
const { YBButton } = yba;

const Panel = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(2.5),
  width: 340,
  padding: theme.spacing(3),
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: 8,
  background: theme.palette.common.white
}));

const LinkButton = styled(ButtonBase)(({ theme }) => ({
  gap: theme.spacing(0.5),
  fontSize: 11.5,
  lineHeight: '16px',
  fontWeight: 600,
  color: theme.palette.primary[600],
  alignSelf: 'flex-start',
  whiteSpace: 'nowrap'
}));

const EMPTY_SIZE = '-';

interface SummaryPanelProps {
  /** Categories that will be collected, in display order. */
  categories: SupportBundleCategoryDef[];
  summary: SupportBundleEstimateSummary | null;
  isEstimating: boolean;
  isEstimateFailed: boolean;
  /** False while the form can't be estimated: an incomplete range, or nothing to collect. */
  canEstimate: boolean;
  asOf: Date | null;
  onRefreshEstimate: () => void;
  onScopeToFewerNodes: () => void;
  onCreate: () => void;
  isCreateDisabled: boolean;
  isCreating: boolean;
}

export const SummaryPanel = ({
  categories,
  summary,
  isEstimating,
  isEstimateFailed,
  canEstimate,
  asOf,
  onRefreshEstimate,
  onScopeToFewerNodes,
  onCreate,
  isCreateDisabled,
  isCreating
}: SummaryPanelProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'universeActions.supportBundleV2' });
  const [showBreakdown, setShowBreakdown] = useState(false);
  const hasEstimate = !isEstimating && !!summary;

  return (
    <Panel data-testid="SupportBundleSummaryPanel">
      <Box display="flex" flexDirection="column" gap={0.5}>
        <Typography variant="subtitle1" color="grey.700">
          {t('summary.estimatedSize')}
        </Typography>
        <Typography variant="h4" color="text.primary" data-testid="SupportBundleSummaryPanel-Size">
          {hasEstimate ? `\u2248 ${formatBytes(summary.totalBytes)}` : EMPTY_SIZE}
        </Typography>
        {!hasEstimate && (
          <Typography
            variant="subtitle1"
            color="grey.700"
            data-testid="SupportBundleSummaryPanel-EstimateStatus"
          >
            {!canEstimate
              ? t('summary.cannotEstimate')
              : isEstimateFailed && !isEstimating
                ? t('summary.estimateFailed')
                : t('summary.estimating')}
          </Typography>
        )}
        {!hasEstimate && canEstimate && isEstimateFailed && !isEstimating && (
          <LinkButton onClick={onRefreshEstimate} data-testid="SupportBundleSummaryPanel-Retry">
            <RefreshIcon />
            <span>{t('summary.refreshEstimate')}</span>
          </LinkButton>
        )}
        {hasEstimate && (
          // The timestamp is long once it carries the time zone; Refresh moves to its own line
          // instead of squeezing into a column beside it.
          <Box display="flex" alignItems="center" flexWrap="wrap" columnGap={1.25} rowGap={0.5}>
            {asOf && (
              <Typography variant="subtitle1" color="grey.700">
                {t('summary.asOf')} {ybFormatDate(asOf, YBTimeFormats.YB_LONG_DATETIME)}
              </Typography>
            )}
            <LinkButton onClick={onRefreshEstimate} data-testid="SupportBundleSummaryPanel-Refresh">
              <RefreshIcon />
              <span>{t('summary.refreshEstimate')}</span>
            </LinkButton>
          </Box>
        )}
      </Box>
      {hasEstimate && summary.isLarge && (
        <YBAlert
          open
          variant={AlertVariant.Warning}
          dataTestId="SupportBundleSummaryPanel-LargeWarning"
          text={
            <Box display="flex" flexDirection="column" gap={1}>
              <span>{t('summary.largeBundleWarning')}</span>
              <LinkButton
                onClick={onScopeToFewerNodes}
                data-testid="SupportBundleSummaryPanel-ScopeNodes"
              >
                {t('summary.scopeToFewerNodes')}
              </LinkButton>
            </Box>
          }
        />
      )}
      <LinkButton
        onClick={() => setShowBreakdown(!showBreakdown)}
        data-testid="SupportBundleSummaryPanel-Breakdown"
      >
        {showBreakdown ? t('summary.hideBreakdown') : t('summary.viewBreakdown')}
      </LinkButton>
      {showBreakdown && (
        <Box display="flex" flexDirection="column" gap={1}>
          {categories.map((category) => (
            <Box key={category.id} display="flex" justifyContent="space-between">
              <Typography variant="subtitle1" color="text.primary">
                {t(`category.${category.id}`)}
              </Typography>
              <Typography variant="subtitle1" color="grey.600">
                {hasEstimate
                  ? formatBytes(summary.categoryBytes[category.id] ?? 0)
                  : t('summary.estimating')}
              </Typography>
            </Box>
          ))}
        </Box>
      )}
      <YBButton
        variant="ybaPrimary"
        size="large"
        fullWidth
        disabled={isCreateDisabled}
        showSpinner={isCreating}
        onClick={onCreate}
        dataTestId="SupportBundleSummaryPanel-Create"
      >
        {t('create.createBundle')}
      </YBButton>
      <Box display="flex" flexDirection="column" gap={0.75}>
        <Typography variant="subtitle1" color="grey.700">
          {t('summary.compressionNote')}
        </Typography>
        <Typography variant="subtitle1" color="grey.700">
          {t('summary.expiryNote')}
        </Typography>
      </Box>
    </Panel>
  );
};
