import { Suspense, useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useEditUniverseContext } from '../EditUniverseUtils';
import { mui, YBButton, YBDropdown } from '@yugabyte-ui-library/core';
import { KeyboardArrowDown } from '@material-ui/icons';
import { MasterServerNodeAllocationModal } from '../master-server/MasterServerNodeAllocationModal';
import { ClusterInstanceCard } from './ClusterInstanceCard';
import { Portal } from '@material-ui/core';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { EditPlacement } from './EditPlacement';
import AddIcon from '@app/redesign/assets/add.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';

const { Box, Typography, MenuItem, Divider, Link } = mui;

interface ModalParams {
  showEditResilienceAndRegionsModal: boolean;
  skipResilienceAndRegionsStep: boolean;
  showMasterServerNodeAllocationModal: boolean;
  selectedPartitionId: string | undefined;
}

export const GeoPartitionPlacementView = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const [modalParams, setModalParams] = useState<ModalParams>({
    showEditResilienceAndRegionsModal: false,
    skipResilienceAndRegionsStep: false,
    showMasterServerNodeAllocationModal: false,
    selectedPartitionId: undefined
  });

  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });

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
            <MenuItem data-test-id="add-read-replica" sx={{ height: 'auto' }}>
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
      {universeData?.spec?.clusters.map((cluster) =>
        cluster.partitions_spec?.map((partition) => (
          <ClusterInstanceCard
            key={partition.name}
            title={partition.name}
            cluster={cluster}
            placement={partition.placement}
            editResilienceAndRegionsClicked={() => {
              setModalParams({
                showEditResilienceAndRegionsModal: true,
                skipResilienceAndRegionsStep: false,
                showMasterServerNodeAllocationModal: false,
                selectedPartitionId: partition.uuid
              });
            }}
            editPlacementClicked={() => {
              setModalParams({
                showEditResilienceAndRegionsModal: true,
                skipResilienceAndRegionsStep: true,
                showMasterServerNodeAllocationModal: false,
                selectedPartitionId: partition.uuid
              });
            }}
          />
        ))
      )}
      <MasterServerNodeAllocationModal
        visible={modalParams.showMasterServerNodeAllocationModal}
        onClose={() =>
          setModalParams((prev) => ({ ...prev, showMasterServerNodeAllocationModal: false }))
        }
      />
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
