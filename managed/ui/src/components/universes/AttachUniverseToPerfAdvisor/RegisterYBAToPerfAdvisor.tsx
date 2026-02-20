import { useQuery } from 'react-query';
import { Box, makeStyles } from '@material-ui/core';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBPanelItem } from '../../panels';
import { CheckPerfAdvisorRegistration } from './CheckPerfAdvisorRegistration';
import {
  PerfAdvisorAPI,
  QUERY_KEY as PERF_ADVISOR_QUERY_KEY
} from '../../../redesign/features/PerfAdvisor/api';
import { AppName } from '../../../redesign/helpers/dtos';
import { isEmptyArray } from '../../../utils/ObjectUtils';

interface RegisterYBAToPerfAdvisorProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
}

const useStyles = makeStyles((theme) => ({
  register: {
    cursor: 'pointer'
  }
}));

export const RegisterYBAToPerfAdvisor = ({
  universeUuid,
  appName,
  timezone
}: RegisterYBAToPerfAdvisorProps) => {
  const helperClasses = useStyles();
  const {
    data: perfAdvisorListData,
    isLoading: isPaListFetchLoading,
    isError: isPaListFetchError,
    isIdle: isPaListFetchIdle
  } = useQuery(PERF_ADVISOR_QUERY_KEY.fetchPerfAdvisorList, () =>
    PerfAdvisorAPI.fetchPerfAdvisorList()
  );

  if (isPaListFetchError) {
    return <YBErrorIndicator customErrorMessage={'Failed to get Performance Advisor details'} />;
  }

  if (isPaListFetchLoading || (isPaListFetchIdle && perfAdvisorListData === undefined)) {
    return <YBLoading />;
  }

  if (isEmptyArray(perfAdvisorListData)) {
    return (
      <YBPanelItem
        body={
          <Box>
            {'Please'}
            <a href={`/config/perfAdvisor/register`} className={helperClasses.register}>
              {' register '}
            </a>
            {'YB Anywhere instance to Performance Advisor Service'}
          </Box>
        }
      />
    );
  }

  return (
    <CheckPerfAdvisorRegistration
      universeUuid={universeUuid}
      appName={appName}
      timezone={timezone}
      apiUrl={`${perfAdvisorListData?.[0]?.paUrl}/api`}
      paUuid={perfAdvisorListData?.[0]?.uuid}
    />
  );
};
