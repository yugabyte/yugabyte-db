import { Suspense, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { mui, YBButton } from '@yugabyte-ui-library/core';
import { toast } from 'react-toastify';
import { MasterServerNodeAllocationModal } from '../master-server/MasterServerNodeAllocationModal';
import { ClusterInstanceCard, ClusterInstanceCardEditMenuItem } from './ClusterInstanceCard';
import { DeleteReadReplicaModal } from './DeleteReadReplicaModal';
import { Portal } from '@material-ui/core';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { EditPlacement } from './EditPlacement';
import AddIcon from '@app/redesign/assets/add.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import { useEditUniverse } from '@app/v2/api/universe/universe';
import { buildGeoPartitionPlacementEditPayload } from './EditPlacementUtils';
import { createErrorMessage } from '@app/utils/ObjectUtils';
import { EditPlacementContextProps } from './EditPlacementContext';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import DeleteOutlineIcon from '@app/redesign/assets/delete2.svg';
import {
  getAddGeoPartitionRoute,
  getAddReadReplicaRoute
} from '../../read-replica/readReplicaUtils';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { useApplyMasterAllocation } from '../hooks/useApplyMasterAllocation';
import { PlacementActionsMenu } from './PlacementActionsMenu';

const { Box, Typography } = mui;

interface ModalParams {
  showEditResilienceAndRegionsModal: boolean;
  skipResilienceAndRegionsStep: boolean;
  showMasterServerNodeAllocationModal: boolean;
  selectedPartitionId: string | undefined;
  /** Partition UUID driving master allocation modal (defaults partition when opened from header). */
  masterAllocationPartitionUUID: string | undefined;
  showDeleteReadReplicaModal: boolean;
}

export const GeoPartitionPlacementView = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const editUniverse = useEditUniverse();
  const [modalParams, setModalParams] = useState<ModalParams>({
    showEditResilienceAndRegionsModal: false,
    skipResilienceAndRegionsStep: false,
    showMasterServerNodeAllocationModal: false,
    selectedPartitionId: undefined,
    masterAllocationPartitionUUID: undefined,
    showDeleteReadReplicaModal: false
  });

  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const universeUUID = universeData?.info?.universe_uuid;
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const readReplicaClusters = getClusterByType(universeData!, ClusterSpecClusterType.ASYNC);

  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const spec = universeData?.spec as Record<string, unknown> | undefined;
  const rawName = String(spec?.name ?? spec?.universeName ?? spec?.universe_name ?? '').trim();
  const universeDisplayName = rawName || universeUuid;

  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeUUID);

  const { applyMasterAllocation, isSubmitting: isMasterAllocSubmitting } = useApplyMasterAllocation({
    universeData,
    providerRegions,
    selectedPartitionUUID: modalParams.masterAllocationPartitionUUID,
    onAfterApplied: () =>
      setModalParams((prev) => ({
        ...prev,
        showMasterServerNodeAllocationModal: false,
        masterAllocationPartitionUUID: undefined
      }))
  });

  const onApplyPlacementChanges = (
    context: EditPlacementContextProps,
    onSuccess?: () => void
  ) => {
    if (!modalParams.selectedPartitionId || !context.resilience) {
      toast.error('Unable to apply placement changes');
      return;
    }

    let clusterUUID: string;
    let partitionsSpec: ReturnType<typeof buildGeoPartitionPlacementEditPayload>['partitionsSpec'];
    try {
      ({ clusterUUID, partitionsSpec } = buildGeoPartitionPlacementEditPayload(
        universeData!,
        modalParams.selectedPartitionId,
        context.resilience,
        context.nodesAndAvailability
      ));
    } catch (e) {
      toast.error(createErrorMessage(e));
      return;
    }

    editUniverse.mutate(
      {
        uniUUID: universeData!.info!.universe_uuid!,
        data: {
          expected_universe_version: -1,
          clusters: [
            {
              uuid: clusterUUID,
              partitions_spec: partitionsSpec
            }
          ]
        }
      },
      {
        onSuccess: (response) => {
          handleEditUniverseSuccess(response.task_uuid);
          onSuccess?.();
          setModalParams((prev) => ({
            ...prev,
            showEditResilienceAndRegionsModal: false,
            skipResilienceAndRegionsStep: false
          }));
        },
        onError(error) {
          toast.error(createErrorMessage(error));
        }
      }
    );
  };

  const openMasterAllocationModal = (partitionUUID?: string) => {
    const partitions = primaryCluster?.partitions_spec ?? [];
    const resolvedUuid =
      partitionUUID ??
      partitions.find((p) => p.default_partition)?.uuid ??
      partitions[0]?.uuid;
    setModalParams((prev) => ({
      ...prev,
      showMasterServerNodeAllocationModal: true,
      masterAllocationPartitionUUID: resolvedUuid
    }));
  };

  const defaultGeoPartitionUuid = useMemo(() => {
    const partitions = primaryCluster?.partitions_spec ?? [];
    return (
      partitions.find((p) => p.default_partition)?.uuid ??
      partitions[0]?.uuid
    );
  }, [primaryCluster?.partitions_spec]);

  const readReplicaEditMenuItems: ClusterInstanceCardEditMenuItem[] | undefined = useMemo(() => {
    if (!readReplicaClusters?.uuid) return undefined;
    return [
      {
        id: 'edit-placement',
        label: t('menuEditPlacement'),
        dataTestId: 'read-replica-edit-placement',
        onClick: () => {
          window.location.href = getAddReadReplicaRoute(universeUuid);
        },
        startIcon: <EditIcon />
      },
      {
        id: 'delete-read-replica',
        label: t('menuDeleteReadReplica'),
        dataTestId: 'read-replica-delete',
        showDividerBefore: true,
        destructive: true,
        onClick: () =>
          setModalParams((prev) => ({ ...prev, showDeleteReadReplicaModal: true })),
        startIcon: <DeleteOutlineIcon />
      }
    ];
  }, [readReplicaClusters?.uuid, t, universeUuid]);

  return (
    <Box>
      <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', mb: 2 }}>
        <Typography variant="h5" sx={{ fontWeight: 600 }}>
          {t('geoParitionPlacementView.title')}
        </Typography>
        <Box sx={{ display: 'flex', gap: 1, alignItems: 'center' }}>
          <YBButton
            startIcon={<AddIcon />}
            variant="secondary"
            dataTestId="addGeoPartition"
            onClick={() => {
              window.location.href = getAddGeoPartitionRoute(universeUuid);
            }}
          >
            {t('geoParitionPlacementView.addGeoPartition')}
          </YBButton>
          <PlacementActionsMenu universeUuid={universeUuid} triggerLabelKey="actions" />
        </Box>
      </Box>
      {primaryCluster &&
        primaryCluster.partitions_spec?.map((partition) => (
          <ClusterInstanceCard
            key={partition.name}
            title={partition.name}
            cluster={primaryCluster}
            parition={partition}
            placement={partition.placement}
            editResilienceAndRegionsClicked={() => {
              setModalParams({
                showEditResilienceAndRegionsModal: true,
                skipResilienceAndRegionsStep: false,
                showMasterServerNodeAllocationModal: false,
                selectedPartitionId: partition.uuid,
                masterAllocationPartitionUUID: undefined,
                showDeleteReadReplicaModal: false
              });
            }}
            editPlacementClicked={() => {
              setModalParams({
                showEditResilienceAndRegionsModal: true,
                skipResilienceAndRegionsStep: true,
                showMasterServerNodeAllocationModal: false,
                selectedPartitionId: partition.uuid,
                masterAllocationPartitionUUID: undefined,
                showDeleteReadReplicaModal: false
              });
            }}
            editMasterServerNodeAllocationClicked={
              partition.uuid === defaultGeoPartitionUuid
                ? () => {
                    openMasterAllocationModal(partition.uuid);
                  }
                : undefined
            }
          />
        ))
      }
      {readReplicaClusters &&
        readReplicaClusters.partitions_spec?.map((partition) => (
          <ClusterInstanceCard
            key={partition.name}
            title={t('readReplica')}
            cluster={readReplicaClusters}
            placement={partition.placement}
            editMenuItems={readReplicaEditMenuItems}
          />
        ))
      }
      <MasterServerNodeAllocationModal
        visible={modalParams.showMasterServerNodeAllocationModal}
        onClose={() =>
          setModalParams((prev) => ({
            ...prev,
            showMasterServerNodeAllocationModal: false,
            masterAllocationPartitionUUID: undefined
          }))
        }
        onApply={applyMasterAllocation}
        isSubmitting={isMasterAllocSubmitting}
        selectedPartitionUUID={modalParams.masterAllocationPartitionUUID}
      />
      {readReplicaClusters?.uuid ? (
        <DeleteReadReplicaModal
          open={modalParams.showDeleteReadReplicaModal}
          onClose={() =>
            setModalParams((prev) => ({ ...prev, showDeleteReadReplicaModal: false }))
          }
          universeUuid={universeUuid}
          clusterUuid={readReplicaClusters.uuid}
          universeDisplayName={universeDisplayName}
        />
      ) : null}
      <Portal container={document.body}>
        <Suspense fallback={<YBLoadingCircleIcon />}>
          <EditPlacement
            visible={modalParams.showEditResilienceAndRegionsModal}
            onHide={() => {
              setModalParams((prev) => ({
                ...prev,
                showEditResilienceAndRegionsModal: false,
                skipResilienceAndRegionsStep: false
              }));
            }}
            skipResilienceAndRegionsStep={modalParams.skipResilienceAndRegionsStep}
            selectedPartitionUUID={modalParams.selectedPartitionId}
            isSubmittingPlacementUpdate={editUniverse.isLoading}
            onSubmit={(ctx) => {
              onApplyPlacementChanges(ctx);
            }}
          />
        </Suspense>
      </Portal>
    </Box>
  );
};
