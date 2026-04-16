import { Trans, useTranslation } from 'react-i18next';
import { useMemo, useState } from 'react';
import { browserHistory } from 'react-router';
import { Box } from '@material-ui/core';
import { PerfAdvisorEntry } from '@yugabytedb/perf-advisor-ui';
import { AppName } from '@app/redesign/helpers/dtos';
import { YBPanelItem } from '../../panels';

interface PerfAdvisorOverviewDashboardProps {
  universeUuid: string;
  timezone: string;
  apiUrl: string;
  registrationStatus: boolean;
}

export const PerfAdvisorOverviewDashboard = ({
  universeUuid,
  timezone,
  apiUrl,
  registrationStatus
}: PerfAdvisorOverviewDashboardProps) => {
  const { t } = useTranslation();

  const [troubleshootUuid, setTroubleshootUuid] = useState<string | null>(null);
  const [queryId, setQueryId] = useState<string | null>(null);

  // This call back function is to handle YBM case to ensure we dont have a full page reload
  const onNavigateToUrl = (url: string) => {
    browserHistory.push(url);
  };

  const onSelectedIssue = (selectedIssueId: string | null) => {
    setTroubleshootUuid(selectedIssueId);
    setQueryId(null);
  };

  const onSelectedQuery = (selectedQueryId: string | null) => {
    setQueryId(selectedQueryId);
    setTroubleshootUuid(null);
  };

  // We use useMemo to prevent re-rendering the PerfAdvisorEntry component when the universeUuid, queryId, or troubleshootUuid changes.
  const memoizedPerfAdvisorEntry = useMemo(() => {
    return (
      <PerfAdvisorEntry
        universeUuid={universeUuid}
        appName={AppName.YBA}
        timezone={timezone}
        apiUrl={apiUrl}
        onSelectedIssue={onSelectedIssue}
        onSelectedQuery={onSelectedQuery}
        onNavigateToUrl={onNavigateToUrl}
      />
    );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [universeUuid, queryId, troubleshootUuid]);

  if (!registrationStatus) {
    return (
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
  }

  return registrationStatus && memoizedPerfAdvisorEntry;
};
