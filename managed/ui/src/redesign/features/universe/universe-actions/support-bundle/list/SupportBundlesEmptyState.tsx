import { useTranslation } from 'react-i18next';
import { mui, yba } from '@yugabyte-ui-library/core';

import ReportIcon from '@app/redesign/assets/approved/report.svg';

const { Box, Typography } = mui;
const { YBButton } = yba;

interface SupportBundlesEmptyStateProps {
  onCreateBundle?: () => void;
}

export const SupportBundlesEmptyState = ({ onCreateBundle }: SupportBundlesEmptyStateProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.supportBundleV2.list'
  });
  return (
    <Box
      display="flex"
      flexDirection="column"
      alignItems="center"
      gap={2}
      padding="60px 120px"
      textAlign="center"
      data-testid="SupportBundlesEmptyState"
    >
      <Box color="grey.900" display="flex">
        <ReportIcon width={48} height={48} />
      </Box>
      <Typography variant="h5" color="text.primary">
        {t('emptyTitle')}
      </Typography>
      <Typography variant="body2" color="grey.700">
        {t('emptyBody')}
      </Typography>
      <YBButton
        variant="ybaPrimary"
        size="large"
        disabled={!onCreateBundle}
        onClick={onCreateBundle}
        dataTestId="SupportBundlesEmptyState-Create"
      >
        {t('emptyCreate')}
      </YBButton>
    </Box>
  );
};
