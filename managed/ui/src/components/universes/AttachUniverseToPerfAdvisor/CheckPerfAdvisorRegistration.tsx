import { useState } from 'react';
import { useQuery } from 'react-query';
import { PerfAdvisorOverviewDashboard } from './PerfAdvisorOverviewDashboard';
import { YBLoading } from '../../common/indicators';
import { AppName } from '../../../redesign/features/PerfAdvisor/PerfAdvisorAnalysisDashboard';
import {
  PerfAdvisorAPI,
  QUERY_KEY as TROUBLESHOOTING_QUERY_KEY
} from '../../../redesign/features/PerfAdvisor/api';

interface CheckPerfAdvisorRegistrationProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
  apiUrl: string;
  tpUuid: string;
}

export const CheckPerfAdvisorRegistration = ({
  universeUuid,
  appName,
  timezone,
  apiUrl,
  tpUuid
}: CheckPerfAdvisorRegistrationProps) => {
  const [registrationStatus, setRegistrationStatus] = useState<boolean>(false);
  const {
    data: universeRegistrationData,
    isLoading: isUniverseRegistrationFetchLoading,
    isIdle: isUniverseRegistrationFetchIdle,
    refetch: refetchUniverseRegistration
  } = useQuery(
    TROUBLESHOOTING_QUERY_KEY.fetchUniverseRegistrationDetails,
    () => PerfAdvisorAPI.fetchUniverseRegistrationDetails(tpUuid, universeUuid),
    {
      onSuccess: (data) => {
        setRegistrationStatus(data.success);
      },
      onError: (error: any) => {
        error.request.status === 404 && setRegistrationStatus(false);
      }
    }
  );

  if (
    isUniverseRegistrationFetchLoading ||
    (isUniverseRegistrationFetchIdle && universeRegistrationData === undefined)
  ) {
    return <YBLoading />;
  }

  return (
    <PerfAdvisorOverviewDashboard
      tpUuid={tpUuid}
      universeUuid={universeUuid}
      appName={appName}
      timezone={timezone}
      apiUrl={apiUrl}
      registrationStatus={registrationStatus}
      refetchUniverseRegistration={refetchUniverseRegistration}
    />
  );
};
