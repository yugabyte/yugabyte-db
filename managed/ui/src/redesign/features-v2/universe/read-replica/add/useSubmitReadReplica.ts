import { useCallback } from 'react';
import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { useAddCluster, useEditUniverse } from '@app/v2/api/universe/universe';
import { getReadOnlyCluster } from '@app/redesign/utils/universeUtils';
import { createErrorMessage } from '@app/redesign/features/universe/universe-form/utils/helpers';
import { AddRRContextProps } from './AddReadReplicaContext';
import {
  mapAddReadReplicaClusterPayload,
  mapEditReadReplicaClusterSpec
} from './addReadReplicaClusterPayload';
import { getReadReplicaExitRoute } from '../readReplicaUtils';
import { EditUniverseTabs } from '../../edit-universe/EditUniverseContext';

/**
 * Shared submit for the Add/Edit Read Replica wizard.
 * Adds a cluster when no read replica exists, otherwise edits the existing ASYNC cluster,
 * then redirects to the universe placement tab. Reused by the Review step (full wizard) and
 * the Regions & AZ step (placement-only edit).
 */
export const useSubmitReadReplica = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });
  const addCluster = useAddCluster();
  const editUniverse = useEditUniverse();

  const submit = useCallback(
    (context: AddRRContextProps, providerRegions: Region[]): Promise<unknown> => {
      const { universeUuid, universeData } = context;
      if (!universeUuid) {
        toast.error(t('validation.universeUuidMissing'));
        return Promise.resolve();
      }

      const existingReadReplicaCluster = getReadOnlyCluster(universeData?.spec?.clusters ?? []);

      const redirect = () => {
        window.location.href = getReadReplicaExitRoute(universeUuid, EditUniverseTabs.PLACEMENT);
      };
      const runMutation = (mutationPromise: Promise<unknown>, errorPrefix: string) =>
        mutationPromise.then(redirect).catch((error) => console.error(errorPrefix, error));

      try {
        if (existingReadReplicaCluster?.uuid) {
          const editPayload = {
            expected_universe_version: -1,
            clusters: [
              mapEditReadReplicaClusterSpec(existingReadReplicaCluster.uuid, context, providerRegions, {
                enforceNumNodesFloor: true
              })
            ]
          };
          return runMutation(
            editUniverse.mutateAsync(
              { uniUUID: universeUuid, data: editPayload },
              {
                onError(error: any) {
                  toast.error(error?.response?.data?.error ?? t('toast.updateRRFailed'));
                }
              }
            ),
            'Update read replica failed:'
          );
        }

        const addPayload = mapAddReadReplicaClusterPayload(context, providerRegions);
        return runMutation(
          addCluster.mutateAsync(
            { uniUUID: universeUuid, data: addPayload },
            {
              onError(error: any) {
                toast.error(error?.response?.data?.error ?? t('toast.addRRFailed'));
              }
            }
          ),
          'Add read replica failed:'
        );
      } catch (e) {
        toast.error(createErrorMessage(e));
        return Promise.resolve();
      }
    },
    [addCluster, editUniverse, t]
  );

  return { submit, isSubmitting: addCluster.isLoading || editUniverse.isLoading };
};
