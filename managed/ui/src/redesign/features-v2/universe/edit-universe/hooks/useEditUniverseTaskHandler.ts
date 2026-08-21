import { useCallback } from 'react';
import { useDispatch } from 'react-redux';
import { useQueryClient } from 'react-query';
import { getGetUniverseQueryKey } from '@app/v2/api/universe/universe';
import { showTaskInDrawer } from '@app/actions/tasks';

/**
 * Returns a callback that invalidates the universe query and (when a task UUID
 * is provided) opens the task drawer. Centralizes the post-mutation handling
 * shared by the placement, hardware, and master-allocation flows.
 */
export const useEditUniverseTaskHandler = (universeUUID?: string) => {
  const queryClient = useQueryClient();
  const dispatch = useDispatch();

  return useCallback(
    (taskUUID?: string) => {
      if (!universeUUID) return;
      void queryClient.invalidateQueries(getGetUniverseQueryKey(universeUUID));
      if (taskUUID) {
        dispatch(showTaskInDrawer(taskUUID));
      }
    },
    [universeUUID, queryClient, dispatch]
  );
};
