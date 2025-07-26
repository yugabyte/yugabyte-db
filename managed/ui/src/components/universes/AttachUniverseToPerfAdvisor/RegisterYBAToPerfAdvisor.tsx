import { useQuery } from 'react-query';
import { Box, makeStyles } from '@material-ui/core';
import { AppName } from '../../../redesign/features/PerfAdvisor/PerfAdvisorAnalysisDashboard';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBPanelItem } from '../../panels';
import { CheckPerfAdvisorRegistration } from './CheckPerfAdvisorRegistration';
import {
  PerfAdvisorAPI,
  QUERY_KEY as TROUBLESHOOTING_QUERY_KEY
} from '../../../redesign/features/PerfAdvisor/api';
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
    isLoading: isTpListFetchLoading,
    isError: isTpListFetchError,
    isIdle: isTpListFetchIdle
  } = useQuery(TROUBLESHOOTING_QUERY_KEY.fetchPerfAdvisorList, () =>
    PerfAdvisorAPI.fetchPerfAdvisorList()
  );

  if (isTpListFetchError) {
    return (
      <YBErrorIndicator customErrorMessage={'Failed to get Troubleshooting Platform details'} />
    );
  }

  if (isTpListFetchLoading || (isTpListFetchIdle && perfAdvisorListData === undefined)) {
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
      apiUrl={`${perfAdvisorListData?.[0]?.tpUrl}/api`}
      tpUuid={perfAdvisorListData?.[0]?.uuid}
    />
  );
};
