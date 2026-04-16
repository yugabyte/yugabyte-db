import { Trans, useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { PerfAdvisorEntry } from '@yugabytedb/perf-advisor-ui';
import { YBPanelItem } from '../../panels';
import { YBButton } from '../../../redesign/components';
import { AppName } from '../../../redesign/features/PerfAdvisor/PerfAdvisorAnalysisDashboard';

const useStyles = makeStyles((theme) => ({
  button: {
    marginLeft: theme.spacing(2),
    marginTop: theme.spacing(-0.5),
    float: 'right'
  }
}));

interface PerfAdvisorOverviewDashboardProps {
  universeUuid: string;
  tpUuid: string;
  appName: AppName;
  timezone: string;
  apiUrl: string;
  registrationStatus: boolean;
  refetchUniverseRegistration: any;
}

export const PerfAdvisorOverviewDashboard = ({
  universeUuid,
  appName,
  tpUuid,
  timezone,
  apiUrl,
  registrationStatus,
  refetchUniverseRegistration
}: PerfAdvisorOverviewDashboardProps) => {
  const helperClasses = useStyles();
  const { t } = useTranslation();

  return registrationStatus ? (
    <PerfAdvisorEntry
      universeUuid={universeUuid}
      appName={appName}
      timezone={timezone}
      apiUrl={apiUrl}
    />
  ) : (
    <YBPanelItem
      body={
        <Box>
          <span>
            <Trans
              i18nKey={t('clusterDetail.troubleshoot.enablePerfAdvisorMsg')}
              components={{ bold: <b /> }}
            />
          </span>
        </Box>
      }
    />
  );
};
