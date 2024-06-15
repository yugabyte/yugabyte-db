import { useState } from 'react';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import _ from 'lodash';
import { SecondaryDashboardEntry } from '@yugabytedb/troubleshoot-ui';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { api, QUERY_KEY } from '../universe/universe-form/utils/api';
import { TroubleshootingAPI, QUERY_KEY as TROUBLESHOOTING_QUERY_KEY } from './api';
import { isNonEmptyString } from '../../../utils/ObjectUtils';

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
  const { t } = useTranslation();

  const [TpData, setTpData] = useState<any>([]);
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const universe = useQuery([QUERY_KEY.fetchUniverse, universeUuid], () =>
    api.fetchUniverse(universeUuid)
  );

  const tpList = useQuery(
    TROUBLESHOOTING_QUERY_KEY.fetchTpList,
    () => TroubleshootingAPI.fetchTpList(),
    {
      onSuccess: (data) => {
        setTpData(data);
      }
    }
  );

  if (tpList.isError || universe.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('clusterDetail.troubleshoot.universeDetailsErrorMessage')}
      />
    );
  }
  if (tpList.isLoading || universe.isLoading || (universe.isIdle && universe.data === undefined)) {
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
        apiUrl={isNonEmptyString(TpData?.[0]?.tpUrl) ? `${TpData[0].tpUrl}/api` : ''}
      />
    </>
  );
};
