import { useState } from 'react';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import _ from 'lodash';
import { MetricsAnalysisEntry } from '@yugabytedb/perf-advisor-ui';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { api, QUERY_KEY } from '../universe/universe-form/utils/api';
import { PerfAdvisorAPI, QUERY_KEY as TROUBLESHOOTING_QUERY_KEY } from './api';
import { isNonEmptyString } from '../../../utils/ObjectUtils';
import { Link } from 'react-router';
import { Box, Typography } from '@material-ui/core';

interface PerfAdvisorAnalysisDashboardProps {
  universeUuid: string;
  troubleshootUuid: string | null;
}

export enum AppName {
  YBA = 'YBA',
  YBM = 'YBM',
  YBD = 'YBD'
}

export const PerfAdvisorAnalysisDashboard = ({
  universeUuid,
  troubleshootUuid
}: PerfAdvisorAnalysisDashboardProps) => {
  const { t } = useTranslation();

  const [paData, setPaData] = useState<any>([]);
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const universe = useQuery([QUERY_KEY.fetchUniverse, universeUuid], () =>
    api.fetchUniverse(universeUuid)
  );

  const perfAdvisorUniverseList = useQuery(
    TROUBLESHOOTING_QUERY_KEY.fetchPerfAdvisorList,
    () => PerfAdvisorAPI.fetchPerfAdvisorList(),
    {
      onSuccess: (data) => {
        setPaData(data);
      }
    }
  );

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
          appName={AppName.YBA}
          universeUuid={universeUuid}
          troubleshootUuid={troubleshootUuid}
          universeData={universe.data as any}
          timezone={currentUserTimezone}
          apiUrl={isNonEmptyString(paData?.[0]?.tpUrl) ? `${paData[0].tpUrl}/api` : ''}
        />
      </Box>
    </>
  );
};
