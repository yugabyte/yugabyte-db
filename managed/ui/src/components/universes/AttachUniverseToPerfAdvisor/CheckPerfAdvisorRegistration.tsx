import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { PerfAdvisorTabs } from '../PerfAdvisor/PerfAdvisorTabs';
import {
  PaRegistrationMode,
  PerfAdvisorAPI,
  QUERY_KEY as PERF_ADVISOR_QUERY_KEY
} from '../../../redesign/features/PerfAdvisor/api';
import { AppName } from '../../../redesign/helpers/dtos';
import { isNonEmptyString } from '@app/utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';

interface CheckPerfAdvisorRegistrationProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
  apiUrl: string;
  paUuid: string;
  // From UniverseDetail's redux registration status, which is per universe and refetched when a
  // PA task completes. The react-query entry below can hold another universe's or a pre-task mode.
  registrationMode?: PaRegistrationMode;
}

export const CheckPerfAdvisorRegistration = ({
  universeUuid,
  appName,
  timezone,
  apiUrl,
  paUuid,
  registrationMode
}: CheckPerfAdvisorRegistrationProps) => {
  const { t } = useTranslation();
  // The universe page has already loaded this universe's registration into redux; start from it
  // so this view doesn't show its own loader while fetching the same thing again.
  const reduxRegistration = useSelector(
    (state: any) => state.universe.universePaRegistrationStatus
  );
  const initialRegistration = getPromiseState(reduxRegistration).isSuccess()
    ? reduxRegistration.data
    : undefined;
  const getUniversePaRegistrationStatus = useQuery(
    [PERF_ADVISOR_QUERY_KEY.fetchUniverseRegistrationDetails, universeUuid],
    () => PerfAdvisorAPI.fetchUniverseRegistrationDetails(universeUuid),
    {
      initialData: initialRegistration,
      enabled: isNonEmptyString(paUuid) && isNonEmptyString(universeUuid)
    }
  );
  const registrationStatus = !!getUniversePaRegistrationStatus.data?.success;

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
        registrationMode={registrationMode}
      />
    )
  );
};
