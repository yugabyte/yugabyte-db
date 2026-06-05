import { useCallback } from 'react';
import { toast } from 'react-toastify';
import { useEditUniverse } from '@app/v2/api/universe/universe';
import { createErrorMessage } from '@app/utils/ObjectUtils';
import { Region } from '@app/redesign/helpers/dtos';
import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { buildMasterAllocationEditPayload } from '../edit-placement/EditPlacementUtils';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import { useEditUniverseTaskHandler } from './useEditUniverseTaskHandler';

interface UseApplyMasterAllocationArgs {
  universeData: Universe | null;
  providerRegions?: Region[];
  selectedPartitionUUID?: string;
  /** Called after the edit-universe mutation succeeds. */
  onAfterApplied?: () => void;
}

interface UseApplyMasterAllocationResult {
  applyMasterAllocation: (
    data: NodeAvailabilityProps,
    instanceSettings?: InstanceSettingProps | null
  ) => void;
  isSubmitting: boolean;
}

/**
 * Encapsulates the master-allocation mutation flow shared by the primary and
 * geo-partition placement views: payload assembly, mutation dispatch, success
 * handling (universe re-fetch + task drawer), and error toasts.
 */
export const useApplyMasterAllocation = ({
  universeData,
  providerRegions,
  selectedPartitionUUID,
  onAfterApplied
}: UseApplyMasterAllocationArgs): UseApplyMasterAllocationResult => {
  const editUniverse = useEditUniverse();
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeData?.info?.universe_uuid);

  const applyMasterAllocation = useCallback(
    (data: NodeAvailabilityProps, instanceSettings?: InstanceSettingProps | null) => {
      if (!providerRegions?.length) {
        toast.error('Unable to apply master allocation changes');
        return;
      }
      try {
        const clusterPayload = buildMasterAllocationEditPayload(
          universeData!,
          providerRegions,
          data,
          selectedPartitionUUID,
          instanceSettings
        );
        editUniverse.mutate(
          {
            uniUUID: universeData!.info!.universe_uuid!,
            data: {
              expected_universe_version: -1,
              clusters: [clusterPayload]
            }
          },
          {
            onSuccess: (response) => {
              handleEditUniverseSuccess(response.task_uuid);
              onAfterApplied?.();
            },
            onError(error) {
              toast.error(createErrorMessage(error));
            }
          }
        );
      } catch (e) {
        toast.error(createErrorMessage(e));
      }
    },
    [
      universeData,
      providerRegions,
      selectedPartitionUUID,
      editUniverse,
      handleEditUniverseSuccess,
      onAfterApplied
    ]
  );

  return {
    applyMasterAllocation,
    isSubmitting: editUniverse.isLoading
  };
};
