import { useTranslation } from 'react-i18next';
import { mui, yba } from '@yugabyte-ui-library/core';

import { getSupportBundlePermissions } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import ReportIcon from '@app/redesign/assets/approved/report.svg';

const { Box, Typography } = mui;
const { YBButton } = yba;

const PERMISSIONS = getSupportBundlePermissions(true);

interface SupportBundlesEmptyStateProps {
  universeUuid: string;
  onCreateBundle?: () => void;
}

export const SupportBundlesEmptyState = ({
  universeUuid,
  onCreateBundle
}: SupportBundlesEmptyStateProps) => {
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
          dataTestId="SupportBundlesEmptyState-Create"
        >
          {t('emptyCreate')}
        </YBButton>
      </RbacValidator>
    </Box>
  );
};
