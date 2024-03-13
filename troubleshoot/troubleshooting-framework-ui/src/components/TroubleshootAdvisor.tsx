import { useState } from 'react';
import { useQuery } from 'react-query';
import { Box } from '@material-ui/core';
import { YBErrorIndicator } from '@yugabytedb/ui-components';
import { PrimaryDashboardData } from './PrimaryDashboardData';
import { TroubleshootAPI, QUERY_KEY } from '../api';
import { Anomaly, AppName } from '../helpers/dtos';

import { ReactComponent as LoadingIcon } from '../assets/loading.svg';
import { useHelperStyles } from './styles';
// import LoadingIcon from '../assets/loading.svg';

interface TroubleshootAdvisorProps {
  universeUuid: string;
  appName: AppName;
  timezone?: string;
  onSelectedIssue?: (troubleshootUuid: string) => void;
}

export const TroubleshootAdvisor = ({
  universeUuid,
  appName,
  timezone,
  onSelectedIssue
}: TroubleshootAdvisorProps) => {
  const classes = useHelperStyles();

  const [anomalyList, setAnomalyList] = useState<Anomaly[] | null>(null);

  const { isLoading, isError, isIdle, refetch: anomaliesRefetch } = useQuery(
    [QUERY_KEY.fetchAnamolies, universeUuid],
    () => TroubleshootAPI.fetchAnamolies(universeUuid),
    {
      enabled: anomalyList === null,
      onSuccess: (data: Anomaly[]) => {
        setAnomalyList(data);
      },
      onError: (error: any) => {
        console.error('Failed to fetch anomalies', error);
      }
    }
  );

  const onFilterByDate = async (startDate: any, endDate: any) => {
    // TODO: Pass startDate and endDate to anomlay API once backend is ready
    await anomaliesRefetch();
  };

  if (isLoading) {
    // return <LoadingIcon className={clsx(classes.icon, classes.inProgressIcon)} />;;
  }
  if (isError || (isIdle && anomalyList === null)) {
    return (
      <Box className={classes.recommendation}>
        <YBErrorIndicator
          customErrorMessage={'Failed to fetch anomalies list, please try again.'}
        />
      </Box>
    );
  }
  return (
    <Box sx={{ width: '100%' }}>
      <Box sx={{ borderBottom: 1, borderColor: 'divider' }}>
        <Box m={2}>
          <PrimaryDashboardData
            anomalyData={anomalyList}
            appName={appName}
            timezone={timezone}
            universeUuid={universeUuid}
            onFilterByDate={onFilterByDate}
            onSelectedIssue={onSelectedIssue}
          />
        </Box>
      </Box>
    </Box>
  );
};
