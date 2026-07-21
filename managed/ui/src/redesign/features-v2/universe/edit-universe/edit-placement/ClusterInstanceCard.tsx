import { FC, Fragment } from 'react';
import type { ReactNode } from 'react';
import { mui, YBButton, YBDropdown, YBTable } from '@yugabyte-ui-library/core';
import EditIcon from '@app/redesign/assets/edit2.svg';
import { KeyboardArrowDown } from '@material-ui/icons';
import { useTranslation } from 'react-i18next';
import {
  ClusterPartitionSpec,
  ClusterPlacementSpec,
  ClusterSpec,
  ClusterSpecClusterType,
  NodeDetailsDedicatedTo,
  PlacementRegion
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  countRegionsAzsAndNodes,
  countMasterNodesInAz,
  getDedicatedCountsForPlacementRegion,
  getDedicatedTserverMasterDisplayCounts,
  getResilientType,
  useEditUniverseContext,
  useIsUniverseReady
} from '../EditUniverseUtils';
import { getFlagFromRegion } from '../../create-universe/helpers/RegionToFlagUtils';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { CloudType } from '@app/redesign/helpers/dtos';

export type ClusterInstanceCardEditMenuItem = {
  id: string;
  label: ReactNode;
  onClick: () => void;
  dataTestId?: string;
  /** When true, inserts a divider before this item (e.g. before a destructive action). */
  showDividerBefore?: boolean;
  destructive?: boolean;
  startIcon?: ReactNode;
  disabled?: Boolean;
};

interface ClusterInstanceCardProps {
  title: React.ReactNode;
  editPlacementClicked?: () => void;
  editResilienceAndRegionsClicked?: () => void;
  editMasterServerNodeAllocationClicked?: () => void;
  cluster: ClusterSpec;
  parition?: ClusterPartitionSpec;
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
  parition,
  editMenuItems
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const { universeData } = useEditUniverseContext();
  const isUniverseReady = useIsUniverseReady();
  if (!universeData) return null;
  const isK8s = placement.cloud_list?.[0]?.code === CloudType.kubernetes;
  const regionStats = countRegionsAzsAndNodes(placement);
  const resilientType = getResilientType(placement, parition?.replication_factor ?? cluster?.replication_factor, t);
  const isReadReplicaCluster = cluster.cluster_type === ClusterSpecClusterType.ASYNC;
  const dedicatedFromSpec = !!cluster.node_spec?.dedicated_nodes;
  const regionList = placement.cloud_list.map((cloud) => cloud.region_list).flat();

  const dedicatedDisplayTotals = getDedicatedTserverMasterDisplayCounts(
    universeData,
    cluster,
    placement ?? cluster.placement_spec!
  );

  const countTotalNodes = (placementRegion: PlacementRegion) => {
    if (dedicatedFromSpec && !isReadReplicaCluster) {
      const { tserver, master } = getDedicatedCountsForPlacementRegion(
        universeData!,
        placementRegion,
        cluster
      );
      return t('totalNodesTServerMaster', {
        total_nodes: tserver + master,
        tservers: tserver,
        masters: master,
        keyPrefix: 'editUniverse.placement'
      });
    }

    let totalNodes = 0;
    placementRegion?.az_list?.forEach((az) => {
      return (totalNodes += az.num_nodes_in_az ?? 0);
    });
    return totalNodes ?? 0;
  };

  const renderExpandDetails = ({ row }: any) => {
    const placementRegion: PlacementRegion = row.original;

    if (!dedicatedFromSpec) {
      return (
        <YBTable
          columns={[{ accessorKey: 'name', header: t('availabilityZone') }, { accessorKey: 'num_nodes_in_az', header: t(isK8s ? 'noOfPods' : 'noOfNodes') }]}
          data={((placementRegion.az_list as unknown) as Record<string, string>[]) ?? []}
          withBorder={false}
          options={{
            pagination: false
          }}
        />
      );
    }
    const columns = [
      { accessorKey: 'name', header: t('availabilityZone') },
      {
        accessorKey: 'tServers',
        header: t('tServers'),
        Cell: ({ row: azRow }: any) => azRow.original?.num_nodes_in_az ?? 0
      }
    ];
    if(!isReadReplicaCluster) {
      columns.push(
        {
          accessorKey: 'masters',
          header: t('masters'),
          Cell: ({ row: azRow }: any) =>
            countMasterNodesInAz(universeData!, azRow.original?.uuid)
        }
      );
    }
    return (
      <YBTable
        columns={columns}
        data={((placementRegion.az_list as unknown) as Record<string, string>[]) ?? []}
        withBorder={false}
        options={{
          pagination: false
        }}
      />
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
                  disabled={!isUniverseReady}
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
              <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_PLACEMENT} isControl >
                <MenuItem
                  key="resilience"
                  data-test-id="edit-placement-add-region"
                  onClick={() => {
                    editResilienceAndRegionsClicked?.();
                  }}
                  disabled={!isUniverseReady}
                >
                  {<EditIcon />}
                  {t('editResilienceAndRegions')}
                </MenuItem>
              </RbacValidator>
              ,
              <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_PLACEMENT} isControl >

                <MenuItem
                  key="nodes-az"
                  data-test-id="edit-placement-auto-balance"
                  onClick={() => {
                    editPlacementClicked?.();
                  }}
                  disabled={!isUniverseReady}
                >
                  {<EditIcon />}
                  {t(isK8s ? 'editPodsAndAvailabilityZones' : 'editNodesAndAvailabilityZones')}
                </MenuItem>
              </RbacValidator>,
              ...(editMasterServerNodeAllocationClicked
                ? [
                  <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl >
                    <MenuItem
                      key="master-alloc"
                      data-test-id="edit-placement-clear-affinities"
                      onClick={() => {
                        editMasterServerNodeAllocationClicked();
                      }}
                      disabled={!isUniverseReady}
                    >
                      {<EditIcon />}
                      {t('editMasterServerNodeAllocation')}
                    </MenuItem>
                  </RbacValidator>
                ]
                : [])
            ]}
        </YBDropdown>
      </Box>
      <StyledClusterInfo>
        {!isReadReplicaCluster && (
          <>
            <div>
              <Typography variant="subtitle1" color="textSecondary">
                {t('resilienceLevel')}
              </Typography>
              <StyledValue>{resilientType}</StyledValue>
            </div>
            <div>
              <Typography variant="subtitle1" color="textSecondary">
                {t('replicationFactor')}
              </Typography>
              <StyledValue>{parition?.replication_factor ?? cluster?.replication_factor ?? '-'}</StyledValue>
            </div>
          </>
        )}
        <div>
          <Typography variant="subtitle1" color="textSecondary">
            {t(isK8s ? 'totalPods' : 'totalNodes')}
          </Typography>
          <StyledValue>
            {dedicatedFromSpec && !isReadReplicaCluster
              ? t('totalNodesTServerMaster', {
                total_nodes: dedicatedDisplayTotals.tserver + dedicatedDisplayTotals.master,
                tservers: dedicatedDisplayTotals.tserver,
                masters: dedicatedDisplayTotals.master,
                keyPrefix: 'editUniverse.placement'
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
          { 
            accessorKey: 'name', 
            header: t('availabilityZone'),
            Cell: ({ cell}: any) => {
              return cell?.row?.original?.az_list?.length ?? '-';
            }
          },
          {
            accessorKey: 'uuid',
            header: t(isK8s ? 'totalPods' : 'totalNodes'),
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
