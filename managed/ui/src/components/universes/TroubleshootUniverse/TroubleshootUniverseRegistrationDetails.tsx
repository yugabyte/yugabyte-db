import { useState } from 'react';
import { useQuery } from 'react-query';
import { TroubleshootUniverse } from './TroubleshootUniverse';
import { YBLoading } from '../../common/indicators';
import { AppName } from '../../../redesign/features/Troubleshooting/TroubleshootingDashboard';
import {
  TroubleshootingAPI,
  QUERY_KEY as TROUBLESHOOTING_QUERY_KEY
} from '../../../redesign/features/Troubleshooting/api';

interface TroubleshootUniverseRegistrationDetailsProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
  apiUrl: string;
  tpUuid: string;
}

export const TroubleshootUniverseRegistrationDetails = ({
  universeUuid,
  appName,
  timezone,
  apiUrl,
  tpUuid
}: TroubleshootUniverseRegistrationDetailsProps) => {
  const [registrationStatus, setRegistrationStatus] = useState<boolean>(false);
  const {
    data: universeRegistrationData,
    isLoading: isUniverseRegistrationFetchLoading,
    isIdle: isUniverseRegistrationFetchIdle,
    refetch: refetchUniverseRegistration
  } = useQuery(
    TROUBLESHOOTING_QUERY_KEY.fetchUniverseRegistrationDetails,
    () => TroubleshootingAPI.fetchUniverseRegistrationDetails(tpUuid, universeUuid),
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
    <TroubleshootUniverse
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
