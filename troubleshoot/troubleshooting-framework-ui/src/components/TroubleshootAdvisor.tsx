import { useState } from 'react';
import { useQuery } from 'react-query';
import { Box, Theme, Typography, makeStyles } from '@material-ui/core';
import { YBErrorIndicator } from '@yugabytedb/ui-components';
import { PrimaryDashboardData } from './PrimaryDashboardData';
import { TroubleshootAPI, QUERY_KEY } from '../api';
import { Anomaly, AppName, Universe } from '../helpers/dtos';

import LoadingIcon from '../assets/loading.svg';

interface TroubleshootAdvisorProps {
  universeUuid: string;
  // TODO: any should be replaced with YBM Node Response
  universeData: Universe | any;
  appName: AppName;
  baseUrl?: string;
  timezone?: string;
}

const useStyles = makeStyles((theme: Theme) => ({
  icon: {
    width: theme.spacing(3),
    height: theme.spacing(3),
    minWidth: theme.spacing(3),
    minHeight: theme.spacing(3)
  },
  inProgressIcon: {
    color: theme.palette.success[700]
  }
}));

export const TroubleshootAdvisor = ({
  universeUuid,
  universeData,
  appName,
  baseUrl,
  timezone
}: TroubleshootAdvisorProps) => {
  const classes = useStyles();
  const [anomalyList, setAnomalyList] = useState<Anomaly[] | null>(null);

  const { isLoading, isError, isIdle } = useQuery(
    [QUERY_KEY.fetchAnamolies, universeUuid],
    () => TroubleshootAPI.fetchAnamolies(universeUuid),
    {
      onSuccess: (data: Anomaly[]) => {
        setAnomalyList(data);
      },
      onError: (error: any) => {
        console.error('Failed to fetch anomalies', error);
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

  return (
    <Box sx={{ width: '100%' }}>
      <Box sx={{ borderBottom: 1, borderColor: 'divider' }}>
        <Box m={2}>
          <PrimaryDashboardData
            anomalyData={anomalyList}
            appName={appName}
            baseUrl={baseUrl}
            timezone={timezone}
          />
        </Box>
      </Box>
    </Box>
  );
};
