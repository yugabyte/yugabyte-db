import { useEffect } from 'react';
import { useQuery } from 'react-query';
import { usePrevious } from 'react-use';
import { AxiosError } from 'axios';

import { PerfAdvisorAPI, QUERY_KEY } from '@app/redesign/features/PerfAdvisor/api';
import { Universe } from '@app/redesign/features/universe/universe-form/utils/dto';
import { getIsKubernetesUniverse } from '@app/utils/UniverseUtils';

import { SupportBundleUniverseContext } from '../model/categories';

const isNotFound = (error: unknown) => (error as AxiosError)?.response?.status === 404;

/** What decides which support bundle components apply to this universe. */
export const useSupportBundleUniverseContext = (
  universe: Universe
): SupportBundleUniverseContext => {
  const universeUuid = universe.universeUUID;
  const paRegistrationQuery = useQuery(
    [QUERY_KEY.fetchUniverseRegistrationDetails, universeUuid],
    () => PerfAdvisorAPI.fetchUniverseRegistrationDetails(universeUuid),
    {
      refetchOnMount: 'always',
      // 404 means the universe isn't registered with Perf Advisor.
      retry: (failureCount, error) => !isNotFound(error) && failureCount < 3
    }
  );

  // Registering with PA is a universe task, so a check made while it runs is stale once it ends.
  const isUpdateInProgress = !!universe.universeDetails.updateInProgress;
  const wasUpdateInProgress = usePrevious(isUpdateInProgress);
  useEffect(() => {
    if (wasUpdateInProgress && !isUpdateInProgress) {
      paRegistrationQuery.refetch();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [isUpdateInProgress]);

  return {
    isK8sUniverse: getIsKubernetesUniverse(universe),
    isPerfAdvisorRegistered: paRegistrationQuery.isSuccess && !!paRegistrationQuery.data?.success
  };
};
