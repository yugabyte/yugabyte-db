import { FC } from 'react';
import { mui, YBButton, YBDropdown, YBTable } from '@yugabyte-ui-library/core';
import EditIcon from '@app/redesign/assets/edit2.svg';
import { KeyboardArrowDown } from '@material-ui/icons';
import { useTranslation } from 'react-i18next';
import {
  ClusterPlacementSpec,
  ClusterSpec,
  NodeDetailsDedicatedTo,
  PlacementRegion
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  countMasterAndTServerNodes,
  countMasterAndTServerNodesByPlacementRegion,
  countRegionsAzsAndNodes,
  getResilientType,
  hasDedicatedNodes,
  useEditUniverseContext
} from '../EditUniverseUtils';
import { getFlagFromRegion } from '../../create-universe/helpers/RegionToFlagUtils';

interface ClusterInstanceCardProps {
  title: React.ReactNode;
  editPlacementClicked?: () => void;
  editResilienceAndRegionsClicked?: () => void;
  editMasterServerNodeAllocationClicked?: () => void;
  cluster: ClusterSpec;
  placement: ClusterPlacementSpec;
}
const { Box, Typography, MenuItem, styled } = mui;

const StyledClusterInfo = styled(Box)(({ theme }) => ({
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  padding: `16px 24px`,
  marginTop: '8px',
  background: '#FBFCFD',
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  height: '76px'
}));

const StyledValue = styled(Typography)({
  marginTop: '8px',
  fontSize: '13px',
  color: '#0B1117',
  fontWeight: 400
});

export const ClusterInstanceCard: FC<ClusterInstanceCardProps> = ({
  editPlacementClicked,
  title,
  editResilienceAndRegionsClicked,
  editMasterServerNodeAllocationClicked,
  cluster,
  placement
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const { universeData } = useEditUniverseContext();
  if (!universeData) return null;
  const regionStats = countRegionsAzsAndNodes(placement);
  const resilientType = getResilientType(regionStats, t);
  const hasDedicatedTo = hasDedicatedNodes(universeData);
  const regionList = placement.cloud_list.map((cloud) => cloud.region_list).flat();

  let dedicatedNodesCount: Partial<Record<NodeDetailsDedicatedTo, number>> = {
    [NodeDetailsDedicatedTo.MASTER]: 0,
    [NodeDetailsDedicatedTo.TSERVER]: 0
  };

  if (hasDedicatedTo) {
    dedicatedNodesCount = countMasterAndTServerNodes(
      universeData,
      placement ?? cluster.placement_spec!
    );
  }
  const countTotalNodes = (placementRegion: PlacementRegion) => {
    if (hasDedicatedTo) {
      return countMasterAndTServerNodesByPlacementRegion(universeData!, placementRegion, true, t);
    }

    let totalNodes = 0;
    placementRegion?.az_list?.forEach((az) => {
      return (totalNodes += az.num_nodes_in_az ?? 0);
    });
    return totalNodes ?? 0;
  };

  const renderExpandDetails = ({ row }: any) => {
    const placementRegion: PlacementRegion = row.original;
    const masterTserverCount: any = countMasterAndTServerNodesByPlacementRegion(
      universeData!,
      placementRegion,
      false,
      t
    );

    return (
      <YBTable
        columns={[
          { accessorKey: 'name', header: t('availabilityZone') },
          ...(!hasDedicatedTo
            ? [{ accessorKey: 'num_nodes_in_az', header: t('noOfNodes') }]
            : [
                {
                  accessorKey: 'tServers',
                  header: t('tServers'),
                  Cell: () => masterTserverCount[NodeDetailsDedicatedTo.TSERVER]! ?? 0
                },
                {
                  accessorKey: 'masters',
                  header: t('masters'),
                  Cell: () => masterTserverCount[NodeDetailsDedicatedTo.MASTER]! ?? 0
                }
              ])
        ]}
        data={((placementRegion.az_list as unknown) as Record<string, string>[]) ?? []}
        withBorder={false}
        options={{
          pagination: false
        }}
      ></YBTable>
    );
  };

  return (
    <Box
      sx={{
        bgcolor: 'background.paper',
        mt: 2,
        px: 3,
        py: 1.25,
        borderRadius: 1,
        display: 'flex',
        flexDirection: 'column',
        gap: 2,
        border: '1px solid #D7DEE4',
        alignSelf: 'stretch'
      }}
    >
      <Box
        sx={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          height: '64px'
        }}
      >
        <Typography sx={{ fontWeight: 600 }} variant="h5">
          {title}
        </Typography>
        <YBDropdown
          dataTestId="edit-placement-actions"
          slotProps={{
            paper: {
              sx: { width: '340px' }
            }
          }}
          origin={
            <YBButton
              dataTestId="edit-placement-edit-button"
              variant="secondary"
              startIcon={<EditIcon />}
              endIcon={<KeyboardArrowDown />}
            >
              {t('edit', { keyPrefix: 'common' })}
            </YBButton>
          }
        >
          <MenuItem
            data-test-id="edit-placement-add-region"
            onClick={() => {
              editResilienceAndRegionsClicked?.();
            }}
          >
            {<EditIcon />}
            {t('editResilienceAndRegions')}
          </MenuItem>
          <MenuItem
            data-test-id="edit-placement-auto-balance"
            onClick={() => {
              editPlacementClicked?.();
            }}
          >
            {<EditIcon />}
            {t('editNodesAndAvailabilityZones')}
          </MenuItem>
          <MenuItem
            data-test-id="edit-placement-clear-affinities"
            onClick={() => {
              editMasterServerNodeAllocationClicked?.();
            }}
          >
            {<EditIcon />}
            {t('editMasterServerNodeAllocation')}
          </MenuItem>
        </YBDropdown>
      </Box>
      <StyledClusterInfo>
        <div>
          <Typography variant="subtitle1" color="textSecondary">
            {t('faultTolerance')}
          </Typography>
          <StyledValue>{resilientType}</StyledValue>
        </div>
        <div>
          <Typography variant="subtitle1" color="textSecondary">
            {t('replicationFactor')}
          </Typography>
          <StyledValue>{cluster?.replication_factor ?? '-'}</StyledValue>
        </div>
        <div>
          <Typography variant="subtitle1" color="textSecondary">
            {t('totalNodes')}
          </Typography>
          <StyledValue>
            {hasDedicatedTo
              ? t('totalNodesTServerMaster', {
                  total_nodes:
                    dedicatedNodesCount[NodeDetailsDedicatedTo.TSERVER]! +
                    dedicatedNodesCount[NodeDetailsDedicatedTo.MASTER]!,
                  tservers: dedicatedNodesCount[NodeDetailsDedicatedTo.TSERVER] ?? 0,
                  masters: dedicatedNodesCount[NodeDetailsDedicatedTo.MASTER] ?? 0
                })
              : regionStats.totalNodes}
          </StyledValue>
        </div>
      </StyledClusterInfo>
      <YBTable
        data={((regionList as unknown) as Record<string, string>[]) ?? []}
        withBorder={false}
        columns={[
          {
            accessorKey: 'code',
            header: t('region'),
            // eslint-disable-next-line react/display-name
            Cell: ({ cell }: any) => (
              <Box sx={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
                <span>{getFlagFromRegion(cell.row.original.code)}</span>
                <span>{cell.row.original.code}</span>
              </Box>
            )
          },
          { accessorKey: 'name', header: t('availabilityZone') },
          {
            accessorKey: 'uuid',
            header: t('totalNodes'),
            Cell: ({ cell }: any) => countTotalNodes(cell.row.original)
          }
        ]}
        options={{
          enableExpanding: true,
          renderDetailPanel: renderExpandDetails
        }}
      />
    </Box>
  );
};
