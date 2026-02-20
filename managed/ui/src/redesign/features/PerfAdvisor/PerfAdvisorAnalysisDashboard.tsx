import { useEffect, useState } from 'react';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import _ from 'lodash';
import { MetricsAnalysisEntry, Universe } from '@yugabytedb/perf-advisor-ui';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { api, QUERY_KEY } from '../universe/universe-form/utils/api';
import { PerfAdvisorAPI, QUERY_KEY as PERF_ADVISOR_QUERY_KEY } from './api';
import { AppName } from '../../helpers/dtos';
import { isNonEmptyString } from '../../../utils/ObjectUtils';
import { browserHistory, Link } from 'react-router';
import { Box, Typography } from '@material-ui/core';

interface PerfAdvisorAnalysisDashboardProps {
  universeUuid: string;
  troubleshootUuid: string | null;
}

export const PerfAdvisorAnalysisDashboard = ({
  universeUuid,
  troubleshootUuid
}: PerfAdvisorAnalysisDashboardProps) => {
  const { t } = useTranslation();

  const [paData, setPaData] = useState<any>([]);
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const [selectedTroubleshootUuid, setSelectedTroubleshootUuid] = useState<string | null>(
    troubleshootUuid
  );
  const [queryId, setQueryId] = useState<string | null>(null);
  const universe = useQuery([QUERY_KEY.fetchUniverse, universeUuid], () =>
    api.fetchUniverse(universeUuid)
  );

  // Initialize troubleshootUuid from URL parameter
  useEffect(() => {
    if (troubleshootUuid) {
      setSelectedTroubleshootUuid(troubleshootUuid);
      setQueryId(null);
    }
  }, [troubleshootUuid]);

  const perfAdvisorUniverseList = useQuery(
    PERF_ADVISOR_QUERY_KEY.fetchPerfAdvisorList,
    () => PerfAdvisorAPI.fetchPerfAdvisorList(),
    {
      onSuccess: (data) => {
        setPaData(data);
      }
    }
  );

  const onSelectedIssue = (selectedIssueId: string | null) => {
    setSelectedTroubleshootUuid(selectedIssueId);
    setQueryId(null);
  };

  const onSelectedQuery = (selectedQueryId: string | null) => {
    setQueryId(selectedQueryId);
    setSelectedTroubleshootUuid(null);
  };

  const onNavigateToUrl = (url: string) => {
    browserHistory.push(url);
  };

  if (perfAdvisorUniverseList.isError || universe.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('clusterDetail.troubleshoot.universeDetailsErrorMessage')}
      />
    );
  }
  if (
    perfAdvisorUniverseList.isLoading ||
    universe.isLoading ||
    (universe.isIdle && universe.data === undefined)
  ) {
    return <YBLoading />;
  }

  return (
    <>
      <div className="perf-advisor-dashboard">
        <h2 className="content-title">
          <Link to={`/universes/${universeUuid}`}> {universe.data.name}</Link>
        </h2>
      </div>
      <Box m={2}>
        <MetricsAnalysisEntry
          universeUuid={universeUuid}
          universeData={(universe.data as unknown) as Universe}
          troubleshootUuid={selectedTroubleshootUuid}
          appName={AppName.YBA}
          onNavigateToUrl={onNavigateToUrl}
          onSelectedIssue={onSelectedIssue}
          onSelectedQuery={onSelectedQuery}
          timezone={currentUserTimezone}
          apiUrl={isNonEmptyString(paData?.[0]?.paUrl) ? `${paData[0].paUrl}/api` : ''}
        />
      </Box>
    </>
  );
};
