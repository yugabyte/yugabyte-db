import { FC, Fragment } from 'react';
import type { ReactNode } from 'react';
import { mui, YBButton, YBDropdown, YBTable } from '@yugabyte-ui-library/core';
import EditIcon from '@app/redesign/assets/edit2.svg';
import { KeyboardArrowDown } from '@material-ui/icons';
import { useTranslation } from 'react-i18next';
import {
  ClusterPlacementSpec,
  ClusterSpec,
  ClusterSpecClusterType,
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

export type ClusterInstanceCardEditMenuItem = {
  id: string;
  label: ReactNode;
  onClick: () => void;
  dataTestId?: string;
  /** When true, inserts a divider before this item (e.g. before a destructive action). */
  showDividerBefore?: boolean;
  destructive?: boolean;
  startIcon?: ReactNode;
};

interface ClusterInstanceCardProps {
  title: React.ReactNode;
  editPlacementClicked?: () => void;
  editResilienceAndRegionsClicked?: () => void;
  editMasterServerNodeAllocationClicked?: () => void;
  cluster: ClusterSpec;
  placement: ClusterPlacementSpec;
  /** When set, replaces the default three-item edit menu (primary / geo primary card). */
  editMenuItems?: ClusterInstanceCardEditMenuItem[];
}
const { Box, Typography, MenuItem, Divider, styled } = mui;

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
  placement,
  editMenuItems
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const { universeData } = useEditUniverseContext();
  if (!universeData) return null;
  const regionStats = countRegionsAzsAndNodes(placement);
  const resilientType = getResilientType(regionStats, t);
  const isReadReplicaCluster = cluster.cluster_type === ClusterSpecClusterType.ASYNC;
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
              sx: editMenuItems
                ? { minWidth: 220, width: 'max-content', py: 1, border: '1px solid #E9EEF2' }
                : { width: '340px' }
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
          {editMenuItems
            ? editMenuItems.map((item) => (
                <Fragment key={item.id}>
                  {item.showDividerBefore ? (
                    <Divider sx={{ borderColor: '#E9EEF2', my: 0.5 }} />
                  ) : null}
                  <MenuItem
                    data-test-id={item.dataTestId}
                    onClick={item.onClick}
                    sx={{
                      px: 2,
                      py: 0.5,
                      alignItems: 'flex-start',
                      color: item.destructive ? '#DA1515' : '#0B1117'
                    }}
                  >
                    <Box
                      sx={{
                        display: 'flex',
                        alignItems: 'flex-start',
                        gap: '4px',
                        width: '100%'
                      }}
                    >
                      {item.startIcon ? (
                        <Box
                          sx={{
                            width: 24,
                            height: 24,
                            flexShrink: 0,
                            display: 'flex',
                            alignItems: 'center',
                            justifyContent: 'center',
                            color: item.destructive ? '#DA1515' : 'inherit',
                            '& svg': { display: 'block' }
                          }}
                        >
                          {item.startIcon}
                        </Box>
                      ) : null}
                      <Typography
                        component="span"
                        sx={{
                          fontSize: '13px',
                          lineHeight: '16px',
                          fontWeight: 400,
                          py: 0.5,
                          color: item.destructive ? '#DA1515' : '#0B1117'
                        }}
                      >
                        {item.label}
                      </Typography>
                    </Box>
                  </MenuItem>
                </Fragment>
              ))
            : [
                <MenuItem
                  key="resilience"
                  data-test-id="edit-placement-add-region"
                  onClick={() => {
                    editResilienceAndRegionsClicked?.();
                  }}
                >
                  {<EditIcon />}
                  {t('editResilienceAndRegions')}
                </MenuItem>,
                <MenuItem
                  key="nodes-az"
                  data-test-id="edit-placement-auto-balance"
                  onClick={() => {
                    editPlacementClicked?.();
                  }}
                >
                  {<EditIcon />}
                  {t('editNodesAndAvailabilityZones')}
                </MenuItem>,
                <MenuItem
                  key="master-alloc"
                  data-test-id="edit-placement-clear-affinities"
                  onClick={() => {
                    editMasterServerNodeAllocationClicked?.();
                  }}
                >
                  {<EditIcon />}
                  {t('editMasterServerNodeAllocation')}
                </MenuItem>
              ]}
        </YBDropdown>
      </Box>
      <StyledClusterInfo>
        {!isReadReplicaCluster && (
          <>
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
          </>
        )}
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
