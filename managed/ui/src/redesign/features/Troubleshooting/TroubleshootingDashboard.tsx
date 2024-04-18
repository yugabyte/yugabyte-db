import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import _ from 'lodash';
import { SecondaryDashboardEntry } from '@yugabytedb/troubleshoot-ui';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { api, QUERY_KEY } from '../universe/universe-form/utils/api';

interface TroubleshootingDashboardProps {
  universeUuid: string;
  troubleshootUuid: string;
}

export enum AppName {
  YBA = 'YBA',
  YBM = 'YBM',
  YBD = 'YBD'
}

export const TroubleshootingDashboard = ({
  universeUuid,
  troubleshootUuid
}: TroubleshootingDashboardProps) => {
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const universe = useQuery([QUERY_KEY.fetchUniverse, universeUuid], () =>
    api.fetchUniverse(universeUuid)
  );

  if (universe.isError) {
    return <YBErrorIndicator customErrorMessage={'Failed to fetch universe details'} />;
  }
  if (universe.isLoading || (universe.isIdle && universe.data === undefined)) {
    return <YBLoading />;
  }

  return (
    <>
      <SecondaryDashboardEntry
        appName={AppName.YBA}
        universeUuid={universeUuid}
        troubleshootUuid={troubleshootUuid}
        universeData={universe.data}
        timezone={currentUserTimezone}
      />
    </>
  );
};
