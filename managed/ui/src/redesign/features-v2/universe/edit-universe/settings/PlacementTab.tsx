import { lazy, Suspense, useMemo, useState } from 'react';
import { toast } from 'react-toastify';
import { mui } from '@yugabyte-ui-library/core';
import { useToggle } from 'react-use';
import { useTranslation } from 'react-i18next';
import { CloudType, ClusterType } from '@app/redesign/helpers/dtos';
import { getClusterByType, getPlacementSpecForCluster, isKubernetesCluster, useEditUniverseContext, useIsUniverseReady } from '../EditUniverseUtils';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { useApplyMasterAllocation } from '../hooks/useApplyMasterAllocation';
import { useEditUniverse } from '@app/v2/api/universe/universe';
import { createErrorMessage } from '@app/utils/ObjectUtils';
import { MasterServerNodeAllocationModal } from '../master-server/MasterServerNodeAllocationModal';
import {
  ClusterInstanceCard,
  ClusterInstanceCardEditMenuItem
} from '../edit-placement/ClusterInstanceCard';
import { DeleteReadReplicaModal } from '../edit-placement/DeleteReadReplicaModal';
import EditIcon from '@app/redesign/assets/edit2.svg';
import DeleteOutlineIcon from '@app/redesign/assets/delete2.svg';
import { getAddReadReplicaRoute } from '../../read-replica/readReplicaUtils';
import { GeoPartitionPlacementView } from '../edit-placement/GeoPartitionPlacementView';
import { EditPlacementContextProps } from '../edit-placement/EditPlacementContext';
import {
  buildGeoPartitionPlacementEditPayload,
  buildPrimaryPlacementEditPayload
} from '../edit-placement/EditPlacementUtils';
import { getNodeCount } from '../../create-universe/CreateUniverseUtils';
import { PlacementActionsMenu } from '../edit-placement/PlacementActionsMenu';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { ClusterPlacementSpec } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { ResilienceFormMode } from '../../create-universe/steps/resilence-regions/dtos';

const EditPlacement = lazy(() =>
  import('../edit-placement/EditPlacement').then((module) => ({
    default: module.EditPlacement
  }))
);

const { Box, Typography, Portal } = mui;

export const PlacementTab = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const primaryCluster = getClusterByType(universeData!, ClusterType.PRIMARY);
  const readReplicaCluster = getClusterByType(universeData!, ClusterType.ASYNC);
  const universeUUID = universeData?.info?.universe_uuid;
  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const spec = universeData?.spec as Record<string, unknown> | undefined;
  const rawName = String(spec?.name ?? spec?.universeName ?? spec?.universe_name ?? '').trim();
  const universeDisplayName = rawName || universeUuid;
  const primaryGeoPartitions = primaryCluster?.partitions_spec ?? [];
  const singlePrimaryGeoPartition =
    primaryGeoPartitions.length === 1 ? primaryGeoPartitions[0] : undefined;
  const isMultiPrimaryGeoPartition = primaryGeoPartitions.length > 1;
  const isUniverseReady = useIsUniverseReady();

  const [showDeleteReadReplicaModal, setShowDeleteReadReplicaModal] = useState(false);

  const isK8s = isKubernetesCluster(primaryCluster);

  const readReplicaPlacementEntries = useMemo(() => {
    const rr = readReplicaCluster;
    if (!rr?.uuid) {
      return [] as { key: string; placement: ClusterPlacementSpec }[];
    }
    if (rr.partitions_spec?.length) {
      return rr.partitions_spec
        .filter((partition): partition is typeof partition & { placement: ClusterPlacementSpec } =>
          Boolean(partition.placement)
        )
        .map((partition) => ({
          key: partition.uuid ?? partition.name,
          placement: partition.placement
        }));
    }
    if (rr.placement_spec) {
      return [{ key: rr.uuid, placement: rr.placement_spec }];
    }
    return [];
  }, [readReplicaCluster]);

  const readReplicaEditMenuItems: ClusterInstanceCardEditMenuItem[] | undefined = useMemo(() => {
    if (!readReplicaCluster?.uuid) return undefined;
    return [
      {
        id: 'edit-placement',
        label: t('menuEditPlacement'),
        dataTestId: 'read-replica-edit-placement',
        onClick: () => {
          window.location.href = getAddReadReplicaRoute(universeUuid);
        },
        startIcon: <EditIcon />,
        disabled: !isUniverseReady
      },
      {
        id: 'delete-read-replica',
        label: t('menuDeleteReadReplica'),
        dataTestId: 'read-replica-delete',
        showDividerBefore: true,
        destructive: true,
        onClick: () => setShowDeleteReadReplicaModal(true),
        startIcon: <DeleteOutlineIcon />,
        disabled: !isUniverseReady
      }
    ];
  }, [isUniverseReady, readReplicaCluster?.uuid, t, universeUuid]);

  const [showEditResilienceAndRegionsModal, setShowEditResilienceAndRegionsModal] = useToggle(
    false
  );

  const [skipResilienceAndRegionsStep, setSkipResilienceAndRegionsStep] = useToggle(false);
  const [showMasterServerNodeAllocationModal, setShowMasterServerNodeAllocationModal] = useToggle(
    false
  );

  const editUniverse = useEditUniverse();
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeUUID);

  const { applyMasterAllocation, isSubmitting: isMasterAllocSubmitting } = useApplyMasterAllocation({
    universeData,
    providerRegions,
    selectedPartitionUUID: singlePrimaryGeoPartition?.uuid,
    onAfterApplied: () => setShowMasterServerNodeAllocationModal(false)
  });

  const editPlacement = (context: EditPlacementContextProps, onSuccess?: () => void) => {
    if (!context.resilience) {
      toast.error('Unable to apply placement changes');
      return;
    }

    let clusterPayload: {
      uuid: string;
      num_nodes?: number;
      placement_spec?: ReturnType<typeof buildPrimaryPlacementEditPayload>['placementSpec'];
      partitions_spec?: ReturnType<typeof buildGeoPartitionPlacementEditPayload>['partitionsSpec'];
    };
    try {
      if (singlePrimaryGeoPartition?.uuid) {
        const { clusterUUID, partitionsSpec } = buildGeoPartitionPlacementEditPayload(
          universeData!,
          singlePrimaryGeoPartition.uuid,
          context.resilience,
          context.nodesAndAvailability
        );
        clusterPayload = {
          uuid: clusterUUID,
          partitions_spec: partitionsSpec
        };
      } else {
        const { clusterUUID, placementSpec } = buildPrimaryPlacementEditPayload(
          universeData!,
          context.resilience,
          context.nodesAndAvailability
        );
        const num_nodes = context.nodesAndAvailability
          ? getNodeCount(context.nodesAndAvailability.availabilityZones)
          : undefined;
        clusterPayload = {
          uuid: clusterUUID,
          ...(num_nodes !== undefined ? { num_nodes } : {}),
          placement_spec: placementSpec
        };
      }
    } catch (e) {
      toast.error(createErrorMessage(e));
      return;
    }

    editUniverse.mutate(
      {
        uniUUID: universeData!.info!.universe_uuid!,
        data: {
          expected_universe_version: -1,
          clusters: [clusterPayload],
          universe_settings: {
            expert_mode : skipResilienceAndRegionsStep ? universeData?.spec?.universe_settings?.expert_mode : context.resilience.resilienceFormMode === ResilienceFormMode.EXPERT_MODE
          }
        }
      },
      {
        onSuccess: (response) => {
          handleEditUniverseSuccess(response.task_uuid);
          onSuccess?.();
          setShowEditResilienceAndRegionsModal(false);
          setSkipResilienceAndRegionsStep(false);
        },
        onError(error) {
          toast.error(createErrorMessage(error));
        }
      }
    );
  };

  if (isMultiPrimaryGeoPartition) {
    return <GeoPartitionPlacementView />;
  }

  return (
    <Box>
      <Box sx={{ justifyContent: 'flex-end', display: 'flex' }}>
        <PlacementActionsMenu
          universeUuid={universeUUID}
          onEditMasterAllocationClick={isK8s ? undefined : () => setShowMasterServerNodeAllocationModal(true)}
          showAddGeoPartition={false}
          readReplicaAlreadyPresent={readReplicaPlacementEntries.length > 0}
        />
      </Box>
      <ClusterInstanceCard
        cluster={primaryCluster!}
        title={
          <Typography sx={{ fontWeight: 600 }} variant="h5">
            {t('primaryCluster')}
          </Typography>
        }
        parition={singlePrimaryGeoPartition}
        placement={singlePrimaryGeoPartition?.placement ?? primaryCluster!.placement_spec!}
        editMasterServerNodeAllocationClicked={isK8s ? undefined : () => {
          setShowMasterServerNodeAllocationModal(true);
        }}
        editPlacementClicked={() => {
          setSkipResilienceAndRegionsStep(true);
          setShowEditResilienceAndRegionsModal(true);
        }}
        editResilienceAndRegionsClicked={() => {
          setShowEditResilienceAndRegionsModal(true);
        }}
      />
      {readReplicaPlacementEntries.length > 0 && readReplicaCluster ? (
        <Box sx={{ mt: 3, display: 'flex', flexDirection: 'column', gap: 2 }}>
          {readReplicaPlacementEntries.map(({ key, placement }) => (
            <ClusterInstanceCard
              key={key}
              title={t('readReplica')}
              cluster={readReplicaCluster}
              placement={placement}
              editMenuItems={readReplicaEditMenuItems}
            />
          ))}
        </Box>
      ) : null}
      {readReplicaCluster?.uuid ? (
        <DeleteReadReplicaModal
          open={showDeleteReadReplicaModal}
          onClose={() => setShowDeleteReadReplicaModal(false)}
          universeUuid={universeUuid}
          clusterUuid={readReplicaCluster.uuid}
          universeDisplayName={universeDisplayName}
        />
      ) : null}
      <Portal container={document.body}>
        <Suspense fallback={<YBLoadingCircleIcon />}>
          <EditPlacement
            visible={showEditResilienceAndRegionsModal}
            onHide={() => {
              setShowEditResilienceAndRegionsModal(false);
              setSkipResilienceAndRegionsStep(false);
            }}
            skipResilienceAndRegionsStep={skipResilienceAndRegionsStep}
            selectedPartitionUUID={singlePrimaryGeoPartition?.uuid}
            isSubmittingPlacementUpdate={editUniverse.isLoading}
            onSubmit={(ctx) => {
              editPlacement(ctx);
            }}
          />
        </Suspense>
      </Portal>
      <MasterServerNodeAllocationModal
        visible={showMasterServerNodeAllocationModal}
        onClose={() => setShowMasterServerNodeAllocationModal(false)}
        onApply={applyMasterAllocation}
        isSubmitting={isMasterAllocSubmitting}
        selectedPartitionUUID={singlePrimaryGeoPartition?.uuid}
      />
    </Box>
  );
};
