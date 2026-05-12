import { Suspense, useMemo, useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { mui, YBButton, YBDropdown } from '@yugabyte-ui-library/core';
import { KeyboardArrowDown } from '@material-ui/icons';
import { MasterServerNodeAllocationModal } from '../master-server/MasterServerNodeAllocationModal';
import { ClusterInstanceCard, ClusterInstanceCardEditMenuItem } from './ClusterInstanceCard';
import { DeleteReadReplicaModal } from './DeleteReadReplicaModal';
import { Portal } from '@material-ui/core';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { EditPlacement } from './EditPlacement';
import AddIcon from '@app/redesign/assets/add.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import DeleteOutlineIcon from '@app/redesign/assets/delete2.svg';
import { getAddReadReplicaRoute } from '../../read-replica/readReplicaUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

const { Box, Typography, MenuItem, Divider, Link } = mui;

interface ModalParams {
  showEditResilienceAndRegionsModal: boolean;
  skipResilienceAndRegionsStep: boolean;
  showMasterServerNodeAllocationModal: boolean;
  selectedPartitionId: string | undefined;
  showDeleteReadReplicaModal: boolean;
}

export const GeoPartitionPlacementView = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const [modalParams, setModalParams] = useState<ModalParams>({
    showEditResilienceAndRegionsModal: false,
    skipResilienceAndRegionsStep: false,
    showMasterServerNodeAllocationModal: false,
    selectedPartitionId: undefined,
    showDeleteReadReplicaModal: false
  });

  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const readReplicaClusters = getClusterByType(universeData!, ClusterSpecClusterType.ASYNC);

  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const spec = universeData?.spec as Record<string, unknown> | undefined;
  const rawName = String(spec?.name ?? spec?.universeName ?? spec?.universe_name ?? '').trim();
  const universeDisplayName = rawName || universeUuid;

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
              window.location.href = `/universes/${universeData?.info?.universe_uuid}/add-geo-partition`;
            }}
          >
            {t('geoParitionPlacementView.addGeoPartition')}
          </YBButton>
          <YBDropdown
            sx={{ width: '340px' }}
            dataTestId="edit-placement-actions"
            slotProps={{
              paper: {
                sx: { width: '340px' }
              }
            }}
            origin={
              <YBButton
                variant="secondary"
                dataTestId="edit-placement-actions-button"
                endIcon={<KeyboardArrowDown />}
              >
                {t('actions', { keyPrefix: 'common' })}
              </YBButton>
            }
          >
            <MenuItem
              data-test-id="edit-placement-clear-affinities"
              onClick={() =>
                setModalParams((prev) => ({ ...prev, showMasterServerNodeAllocationModal: true }))
              }
            >
              <Box sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}>
                <EditIcon />
              </Box>
              {t('editMasterServerNodeAllocation')}
            </MenuItem>
            <Divider />
            <MenuItem
              data-test-id="add-read-replica"
              sx={{ height: 'auto' }}
              onClick={() => {
                window.location.href = getAddReadReplicaRoute(universeData?.info?.universe_uuid);
              }}
            >
              <Box
                sx={{ display: 'flex', alignItems: 'flex-start', flexDirection: 'row', gap: '4px' }}
              >
                <div>
                  <AddIcon />
                </div>
                <Box sx={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
                  {t('addReadReplica')}
                  <Typography
                    variant="subtitle1"
                    color="textSecondary"
                    sx={{ whiteSpace: 'initial' }}
                  >
                    <Trans
                      t={t}
                      i18nKey={'addReadReplicaHelpText'}
                      style={{ lineHeight: '16px' }}
                    />
                  </Typography>
                </Box>
              </Box>
            </MenuItem>
          </YBDropdown>
        </Box>
      </Box>
      {primaryCluster &&
        primaryCluster.partitions_spec?.map((partition) => (
          <ClusterInstanceCard
            key={partition.name}
            title={partition.name}
            cluster={primaryCluster}
            placement={partition.placement}
            editResilienceAndRegionsClicked={() => {
              setModalParams({
                showEditResilienceAndRegionsModal: true,
                skipResilienceAndRegionsStep: false,
                showMasterServerNodeAllocationModal: false,
                selectedPartitionId: partition.uuid,
                showDeleteReadReplicaModal: false
              });
            }}
            editPlacementClicked={() => {
              setModalParams({
                showEditResilienceAndRegionsModal: true,
                skipResilienceAndRegionsStep: true,
                showMasterServerNodeAllocationModal: false,
                selectedPartitionId: partition.uuid,
                showDeleteReadReplicaModal: false
              });
            }}
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
          setModalParams((prev) => ({ ...prev, showMasterServerNodeAllocationModal: false }))
        }
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
              setModalParams((prev) => ({ ...prev, showEditResilienceAndRegionsModal: false }));
              setModalParams((prev) => ({ ...prev, skipResilienceAndRegionsStep: false }));
            }}
            skipResilienceAndRegionsStep={modalParams.skipResilienceAndRegionsStep}
            selectedPartitionUUID={modalParams.selectedPartitionId}
            onSubmit={() => {
              //TODO
            }}
          />
        </Suspense>
      </Portal>
    </Box>
  );
};
