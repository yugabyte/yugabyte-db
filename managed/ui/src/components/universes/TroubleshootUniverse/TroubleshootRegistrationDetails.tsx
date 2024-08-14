import { useQuery } from 'react-query';
import { Box, makeStyles } from '@material-ui/core';
import { AppName } from '../../../redesign/features/Troubleshooting/TroubleshootingDashboard';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBPanelItem } from '../../panels';
import { TroubleshootUniverseRegistrationDetails } from './TroubleshootUniverseRegistrationDetails';
import {
  TroubleshootingAPI,
  QUERY_KEY as TROUBLESHOOTING_QUERY_KEY
} from '../../../redesign/features/Troubleshooting/api';
import { isEmptyArray } from '../../../utils/ObjectUtils';

interface TroubleshootRegistrationDetailsProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
}

const useStyles = makeStyles((theme) => ({
  register: {
    cursor: 'pointer'
  }
}));

export const TroubleshootRegistrationDetails = ({
  universeUuid,
  appName,
  timezone
}: TroubleshootRegistrationDetailsProps) => {
  const helperClasses = useStyles();
  const {
    data: TpListData,
    isLoading: isTpListFetchLoading,
    isError: isTpListFetchError,
    isIdle: isTpListFetchIdle
  } = useQuery(TROUBLESHOOTING_QUERY_KEY.fetchTpList, () => TroubleshootingAPI.fetchTpList());

  if (isTpListFetchError) {
    return (
      <YBErrorIndicator customErrorMessage={'Failed to get Troubleshooting Platform details'} />
    );
  }

  if (isTpListFetchLoading || (isTpListFetchIdle && TpListData === undefined)) {
    return <YBLoading />;
  }

  if (isEmptyArray(TpListData)) {
    return (
      <YBPanelItem
        body={
          <Box>
            {'Please'}
            <a href={`/config/troubleshoot/config`} className={helperClasses.register}>
              {' register '}
            </a>
            {'YB Anywhere instance to Troubleshooting Platform Service'}
          </Box>
        }
      />
    );
  }

  return (
    <TroubleshootUniverseRegistrationDetails
      universeUuid={universeUuid}
      appName={appName}
      timezone={timezone}
      apiUrl={`${TpListData?.[0]?.tpUrl}/api`}
      tpUuid={TpListData?.[0]?.uuid}
    />
  );
};
