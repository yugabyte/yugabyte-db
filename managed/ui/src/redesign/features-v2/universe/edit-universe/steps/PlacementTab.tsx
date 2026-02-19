import { lazy, Suspense } from 'react';
import { toast } from 'react-toastify';
import { mui, YBButton, YBDropdown } from '@yugabyte-ui-library/core';
import { useToggle } from 'react-use';
import { Trans, useTranslation } from 'react-i18next';
import { ClusterType } from '@app/redesign/helpers/dtos';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { KeyboardArrowDown } from '@material-ui/icons';
import { useEditUniverse } from '@app/v2/api/universe/universe';
import { createErrorMessage } from '@app/utils/ObjectUtils';
import { MasterServerNodeAllocationModal } from '../master-server/MasterServerNodeAllocationModal';
import { ClusterInstanceCard } from '../edit-placement/ClusterInstanceCard';
import { GeoPartitionPlacementView } from '../edit-placement/GeoPartitionPlacementView';
import { getExistingGeoPartitions } from '../../geo-partition/add/AddGeoPartitionUtils';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import { getPlacementRegions } from '../../create-universe/CreateUniverseUtils';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import AddIcon from '@app/redesign/assets/add.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';

const EditPlacement = lazy(() =>
  import('../edit-placement/EditPlacement').then((module) => ({
    default: module.EditPlacement
  }))
);

const { Box, MenuItem, Typography, Divider, Link, Portal } = mui;

export const PlacementTab = () => {
  const { universeData } = useEditUniverseContext();
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const primaryCluster = getClusterByType(universeData!, ClusterType.PRIMARY);

  const [showEditResilienceAndRegionsModal, setShowEditResilienceAndRegionsModal] = useToggle(
    false
  );

  const [skipResilienceAndRegionsStep, setSkipResilienceAndRegionsStep] = useToggle(false);
  const [showMasterServerNodeAllocationModal, setShowMasterServerNodeAllocationModal] = useToggle(
    false
  );

  const editUniverse = useEditUniverse();

  const editPlacement = (resilience: ResilienceAndRegionsProps) => {
    const regionList = getPlacementRegions(resilience!);
    const cloudList = primaryCluster!.placement_spec!.cloud_list[0];
    editUniverse.mutate(
      {
        uniUUID: universeData!.info!.universe_uuid!,
        data: {
          expected_universe_version: -1,
          clusters: [
            {
              uuid: primaryCluster!.uuid!,
              placement_spec: {
                cloud_list: [
                  {
                    code: cloudList.code,
                    default_region: cloudList.default_region,
                    masters_in_default_region: cloudList.masters_in_default_region,
                    uuid: cloudList.uuid,
                    region_list: regionList
                  }
                ]
              }
            }
          ]
        }
      },
      {
        onSuccess: () => {
          setShowEditResilienceAndRegionsModal(false);
          setSkipResilienceAndRegionsStep(false);
        },
        onError(error) {
          toast.error(createErrorMessage(error));
        }
      }
    );
  };

  const hasGeoPartitions = getExistingGeoPartitions(universeData!).length > 0;

  if (hasGeoPartitions) {
    return <GeoPartitionPlacementView />;
  }

  return (
    <Box>
      <Box sx={{ justifyContent: 'flex-end', display: 'flex' }}>
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
              {t('actions')}
            </YBButton>
          }
        >
          <MenuItem
            data-test-id="edit-placement-clear-affinities"
            onClick={() => setShowMasterServerNodeAllocationModal(true)}
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
                  <Trans t={t} i18nKey={'addReadReplicaHelpText'} style={{ lineHeight: '16px' }} />
                </Typography>
              </Box>
            </Box>
          </MenuItem>
          <Divider />
          <MenuItem
            data-test-id="add-geo-partition"
            sx={{ height: 'auto' }}
            onClick={() => {
              window.location.href = `/universes/${universeData?.info?.universe_uuid}/add-geo-partition`;
            }}
          >
            <Box
              sx={{ display: 'flex', alignItems: 'flex-start', flexDirection: 'row', gap: '4px' }}
            >
              <div>
                <AddIcon />
              </div>
              <Box sx={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
                {t('addGeoPartition')}
                <Typography
                  variant="subtitle1"
                  color="textSecondary"
                  sx={{ whiteSpace: 'initial' }}
                >
                  <Trans
                    t={t}
                    i18nKey={'geoPartitionHelpText'}
                    components={{ a: <Link /> }}
                    style={{ lineHeight: '16px' }}
                  />
                </Typography>
              </Box>
            </Box>
          </MenuItem>
        </YBDropdown>
      </Box>
      <ClusterInstanceCard
        cluster={primaryCluster!}
        title={
          <Typography sx={{ fontWeight: 600 }} variant="h5">
            {t('clusterPlacement')}
          </Typography>
        }
        placement={primaryCluster!.placement_spec!}
        editMasterServerNodeAllocationClicked={() => {
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
      <Portal container={document.body}>
        <Suspense fallback={<YBLoadingCircleIcon />}>
          <EditPlacement
            visible={showEditResilienceAndRegionsModal}
            onHide={() => {
              setShowEditResilienceAndRegionsModal(false);
              setSkipResilienceAndRegionsStep(false);
            }}
            skipResilienceAndRegionsStep={skipResilienceAndRegionsStep}
            onSubmit={(ctx) => {
              editPlacement(ctx.resilience!);
            }}
          />
        </Suspense>
      </Portal>
      <MasterServerNodeAllocationModal
        visible={showMasterServerNodeAllocationModal}
        onClose={() => setShowMasterServerNodeAllocationModal(false)}
      />
    </Box>
  );
};
