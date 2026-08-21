import { useQueryClient } from 'react-query';
import { useDispatch } from 'react-redux';

import { fetchUniverseInfo, fetchUniverseInfoResponse } from '@app/actions/universe';
import { taskQueryKey, universeQueryKey } from './api';
import { TaskType } from '../features/tasks/dtos';
import { SortDirection } from '../utils/dtos';
import {
  fetchCustomerTasksFailure,
  fetchCustomerTasks,
  fetchCustomerTasksSuccess
} from '@app/actions/tasks';

export const useRefreshUniverseDetailsCache = (universeUuid: string) => {
  const queryClient = useQueryClient();
  const dispatch = useDispatch();

  return () => {
    queryClient.invalidateQueries(universeQueryKey.detail(universeUuid));
    queryClient.invalidateQueries(universeQueryKey.detailsV2(universeUuid));

    // Many legacy components still depend on the universe info from redux store,
    // so we need to refresh it.
    dispatch(fetchUniverseInfo(universeUuid) as any).then((response: any) => {
      dispatch(fetchUniverseInfoResponse(response.payload));
    });
  };
};

export const useRefreshUniverseTasksCache = (universeUuid: string) => {
  const queryClient = useQueryClient();
  const dispatch = useDispatch();
  const refreshUniverseDetailsCache = useRefreshUniverseDetailsCache(universeUuid);

  return () => {
    queryClient.invalidateQueries(taskQueryKey.universe(universeUuid));

    // Many global task components depend on the customer tasks list, so we need to refresh it.
    dispatch(fetchCustomerTasks() as any).then((response: any) => {
      if (!response.error) {
        dispatch(fetchCustomerTasksSuccess(response.payload));
      } else {
        dispatch(fetchCustomerTasksFailure(response.payload));
      }
    });
    setTimeout(() => {
      // Universe details are not updated immediately. Adding a small delay gives little bit
      // of time for the task to be picked up and the universe to be updated.
      refreshUniverseDetailsCache();
    }, 2000);
  };
};

export const useRefreshSoftwareUpgradeTasksCache = (universeUuid: string) => {
  const queryClient = useQueryClient();
  const refreshUniverseTasksCache = useRefreshUniverseTasksCache(universeUuid);

  const getPagedSoftwareUpgradeTasksRequest = {
    direction: SortDirection.DESC,
    filter: {
      typeList: [TaskType.SOFTWARE_UPGRADE],
      targetUUIDList: [universeUuid]
    }
  };

  return () => {
    queryClient.invalidateQueries(taskQueryKey.paged(getPagedSoftwareUpgradeTasksRequest));
    refreshUniverseTasksCache();
  };
};
