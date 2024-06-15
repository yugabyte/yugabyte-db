import { useState } from 'react';
import { useQuery } from 'react-query';
import { Box, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { YBErrorIndicator, isNonEmptyArray } from '@yugabytedb/ui-components';
import { PrimaryDashboardData } from './PrimaryDashboardData';
import { TroubleshootAPI, QUERY_KEY } from '../api';
import { Anomaly, AppName } from '../helpers/dtos';

import { ReactComponent as LoadingIcon } from '../assets/loading.svg';
import { useHelperStyles } from './styles';

interface TroubleshootAdvisorProps {
  universeUuid: string;
  appName: AppName;
  timezone?: string;
  hostUrl?: string;
  onSelectedIssue?: (troubleshootUuid: string) => void;
}

const useStyles = makeStyles((theme) => ({
  inProgressIcon: {
    color: '#1A44A5'
  },
  icon: {
    height: '40px',
    width: '40px'
  },
  loadingBox: {
    position: 'fixed',
    left: '50%',
    top: '50%',
    width: '100%',
    height: '100%'
  }
}));

export const TroubleshootAdvisor = ({
  universeUuid,
  appName,
  timezone,
  hostUrl,
  onSelectedIssue
}: TroubleshootAdvisorProps) => {
  const helperClasses = useHelperStyles();
  const classes = useStyles();

  const [anomalyList, setAnomalyList] = useState<Anomaly[] | null>(null);
  const [startDateTime, setDateStartTime] = useState<Date | null>(null);
  const [endDateTime, setDateEndTime] = useState<Date | null>(null);

  const { isLoading, isError, isIdle, refetch: anomaliesRefetch } = useQuery(
    [QUERY_KEY.fetchAnamolies, universeUuid],
    () => TroubleshootAPI.fetchAnamolies(universeUuid, startDateTime, endDateTime, hostUrl),
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
    setDateEndTime(endDate);
    setDateStartTime(startDate);
    // TODO: Pass startDate and endDate to anomlay API once backend is ready
    await anomaliesRefetch();
  };

  if (isLoading) {
    return (
      <Box className={classes.loadingBox}>
        <LoadingIcon className={clsx(classes.icon, classes.inProgressIcon)} />
      </Box>
    );
  }
  if (isError || (isIdle && anomalyList === null)) {
    return (
      <Box className={helperClasses.recommendation}>
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
          {isNonEmptyArray(anomalyList) ? (
            <PrimaryDashboardData
              anomalyData={anomalyList}
              appName={appName}
              timezone={timezone}
              universeUuid={universeUuid}
              onFilterByDate={onFilterByDate}
              onSelectedIssue={onSelectedIssue}
            />
          ) : (
            <Box className={helperClasses.recommendation}>
              <span>{'There are no issues with the current universe'}</span>
            </Box>
          )}
        </Box>
      </Box>
    </Box>
  );
};
