import { useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { PerfAdvisorTabs } from '../PerfAdvisor/PerfAdvisorTabs';
import {
  PerfAdvisorAPI,
  QUERY_KEY as PERF_ADVISOR_QUERY_KEY
} from '../../../redesign/features/PerfAdvisor/api';
import { AppName } from '../../../redesign/helpers/dtos';
import { isNonEmptyString } from '@app/utils/ObjectUtils';

interface CheckPerfAdvisorRegistrationProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
  apiUrl: string;
  paUuid: string;
}

export const CheckPerfAdvisorRegistration = ({
  universeUuid,
  appName,
  timezone,
  apiUrl,
  paUuid
}: CheckPerfAdvisorRegistrationProps) => {
  const { t } = useTranslation();
  const [registrationStatus, setRegistrationStatus] = useState<boolean>(false);
  const getUniversePaRegistrationStatus = useQuery(
    PERF_ADVISOR_QUERY_KEY.fetchUniverseRegistrationDetails,
    () => PerfAdvisorAPI.fetchUniverseRegistrationDetails(universeUuid),
    {
      onSuccess: (data) => {
        setRegistrationStatus(data.success);
      },
      onError: (error: any) => {
        error.request.status === 404 && setRegistrationStatus(false);
      },
      enabled: isNonEmptyString(paUuid) && isNonEmptyString(universeUuid)
    }
  );

  if (
    getUniversePaRegistrationStatus.isLoading ||
    (getUniversePaRegistrationStatus.isIdle && getUniversePaRegistrationStatus.data === undefined)
  ) {
    return <YBLoading />;
  }

  if (getUniversePaRegistrationStatus.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('clusterDetail.troubleshoot.perfAdvisorRegistrationFailure')}
      />
    );
  }

  return (
    getUniversePaRegistrationStatus.isSuccess && (
      <PerfAdvisorTabs
        universeUuid={universeUuid}
        timezone={timezone}
        apiUrl={apiUrl}
        registrationStatus={registrationStatus}
      />
    )
  );
};
