import { useState } from 'react';
import { useQuery } from 'react-query';
import { Box, Typography } from '@material-ui/core';
import _ from 'lodash';
import { YBBreadcrumb } from '../common/YBBreadcrumb';
import { SecondaryDashboardData } from './SecondaryDashboardData';
import { ANOMALY_DATA_LIST } from './MockAnomalyData';
import { QUERY_KEY, TroubleshootAPI } from '../api';
import { Anomaly, AppName, GraphQuery, Universe } from '../helpers/dtos';
// import { YBErrorIndicator, YBLoading } from '../../../../components/common/indicators';

import LoadingIcon from '../assets/loading.svg';

export interface SecondaryDashboardEntryProps {
  universeUuid: string;
  troubleshootUuid: string;
  // TODO: any should be replaced with YBM Node Response
  universeData: Universe | any;
  appName: AppName;
  url?: string;
  timezone?: string;
}

export const SecondaryDashboardEntry = ({
  universeUuid,
  troubleshootUuid,
  universeData,
  appName,
  url,
  timezone
}: SecondaryDashboardEntryProps) => {
  const [userSelectedAnomaly, setUserSelectedAnomaly] = useState<Anomaly | null>(null);
  const [graphRequestParams, setGraphRequestParams] = useState<GraphQuery[] | null>(null);

  const splitUrl = url?.split(troubleshootUuid);
  const redirectUrl = splitUrl?.[0];

  const { isLoading, isError, isIdle } = useQuery(
    [QUERY_KEY.fetchAnamolies, universeUuid],
    () => TroubleshootAPI.fetchAnamolies(universeUuid),
    {
      onSuccess: (anomalyListdata: Anomaly[]) => {
        const selectedAnomalyData = anomalyListdata.find(
          (anomaly: Anomaly) => anomaly.uuid === troubleshootUuid
        );
        setUserSelectedAnomaly(selectedAnomalyData!);
      },
      onError: (error: any) => {
        console.error('Failed to fetch Anomalies', error);
      }
    }
  );

  // TODO: Display Error and Loading indicator based on API response

  // if (isLoading) {
  //   return <YBErrorIndicator customErrorMessage={'Failed to fetch anomalies list'} />;
  // }
  // if (isError || (isIdle && anomalyData === undefined)) {
  //   return <LoadingIcon />;
  // }

  const selectedAnomaly = ANOMALY_DATA_LIST?.find(
    (anomaly: Anomaly) => anomaly.uuid === troubleshootUuid
  );

  return (
    <Box>
      <Typography variant="h2" className="content-title">
        {appName === AppName.YBA ? (
          <YBBreadcrumb to={`/universes/${universeUuid}/troubleshoot`}>
            {'Troubleshoot'}
          </YBBreadcrumb>
        ) : (
          <>
            <a href={redirectUrl}>{'Troubleshoot'}</a>
            <i className="fa fa-angle-right fa-fw"></i>
          </>
        )}
      </Typography>
      <SecondaryDashboardData
        // TODO: Once API works, pass userSelectedAnomaly to anomalyData
        anomalyData={selectedAnomaly}
        universeData={universeData}
        universeUuid={universeUuid}
        appName={appName}
        timezone={timezone}
      />
    </Box>
  );
};
