import { useTranslation } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { PerfAdvisorOverviewDashboard } from '../AttachUniverseToPerfAdvisor/PerfAdvisorOverviewDashboard';
import { PaRegistrationMode } from '../../../redesign/features/PerfAdvisor/api';
import { isPaDrilldownUrl } from '../AttachUniverseToPerfAdvisor/perfAdvisorUrlHelpers';

interface PerfAdvisorTabsProps {
  universeUuid: string;
  timezone: string;
  apiUrl: string;
  registrationStatus: boolean;
  registrationMode?: PaRegistrationMode;
}

export const PerfAdvisorTabs = ({
  universeUuid,
  timezone,
  apiUrl,
  registrationStatus,
  registrationMode
}: PerfAdvisorTabsProps) => {
  const { t } = useTranslation();

  // In online mode this collector scrapes the universe but keeps nothing - every view below reads
  // local data, so they would all render empty. Say where the data went instead.
  if (registrationMode === PaRegistrationMode.ONLINE) {
    return (
      <Box p={4} data-testid="PerfAdvisorTabs-OnlineMode">
        <Typography variant="body1">{t('clusterDetail.troubleshoot.onlineModeTitle')}</Typography>
        <Box mt={1}>
          <Typography variant="body2" color="textSecondary">
            {t('clusterDetail.troubleshoot.onlineModeSubText')}
          </Typography>
        </Box>
      </Box>
    );
  }

  // Key a drilldown apart from the dashboard: going Back from a drilldown then remounts the
  // dashboard, which reads its state from the URL again instead of keeping the drilldown open.
  return (
    <Box>
      <PerfAdvisorOverviewDashboard
        key={isPaDrilldownUrl() ? 'drilldown' : 'dashboard'}
        universeUuid={universeUuid}
        timezone={timezone}
        apiUrl={apiUrl}
        registrationStatus={registrationStatus}
        registrationMode={registrationMode}
      />
    </Box>
  );
};
