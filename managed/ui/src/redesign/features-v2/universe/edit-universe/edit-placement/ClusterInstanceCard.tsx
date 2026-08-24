import { FC, Fragment, useState } from 'react';
import type { ReactNode } from 'react';
import { mui, YBButton, YBDropdown, YBTable, YBTag } from '@yugabyte-ui-library/core';
import { KeyboardArrowDown } from '@material-ui/icons';
import { useTranslation } from 'react-i18next';
import {
  ClusterPartitionSpec,
  ClusterPlacementSpec,
  ClusterSpec,
  ClusterSpecClusterType,
  NodeDetailsDedicatedTo,
  PlacementAZ,
  PlacementRegion
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  AZ_NOT_PREFERRED,
  AZ_PREFFERED_HIGHEST_RANK
} from '../../create-universe/helpers/constants';
import {
  countRegionsAzsAndNodes,
  countNodesInAzByType,
  getDedicatedCountsForPlacementRegion,
  getDedicatedTserverMasterDisplayCounts,
  getResilientType,
  useEditUniverseContext,
  useIsUniverseReady,
  withUniverseResource
} from '../EditUniverseUtils';
import { getFlagFromRegion } from '../../create-universe/helpers/RegionToFlagUtils';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { CloudType } from '@app/redesign/helpers/dtos';
import { PreferredInfoModal } from '@app/redesign/features-v2/universe/create-universe/steps/nodes-availability/PrefferedInfoModal';

import EditIcon from '@app/redesign/assets/edit2.svg';
import PreferredRankStarIcon from '@app/redesign/assets/preferred-rank-star.svg';
import HelpCircleIcon from '@app/redesign/assets/help-circle.svg';

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
const { Box, Typography, MenuItem, Divider, styled, useTheme } = mui;

const StyledClusterInfo = styled(Box)(({ theme }) => ({
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  padding: `16px 24px`,
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
  fontWeight: 400,
  lineHeight: '16px'
});

const NestedAzTable = styled(Box)(({ theme }) => ({
  width: '100%',
  '& tbody tr:not(:last-of-type) td, & thead th': {
    boxShadow: `inset 0 -1px 0 ${theme.palette.grey[200]}`
  }
}));

const nestedAzTableOptions = {
  pagination: false,
  muiTableProps: { sx: { width: '100%' } }
};

const regionTableDetailPanelProps = ({ row }: { row: { getIsExpanded: () => boolean } }) => ({
  sx: {
    padding: '0 !important',
    overflow: 'hidden !important',
    display: row.getIsExpanded() ? 'table-cell' : 'none !important',
    '& [class*="MuiTableCell-root"]': {
      height: 'auto !important',
      minHeight: 'unset !important',
      maxHeight: 'none !important',
      lineHeight: 'inherit !important',
      borderBottom: 'none !important'
    }
  }
});

const isAzPreferred = (az: PlacementAZ) =>
  az.leader_affinity === true && (az.leader_preference ?? AZ_NOT_PREFERRED) > AZ_NOT_PREFERRED;

const PreferredAzTag: FC<{ az: PlacementAZ }> = ({ az }) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const theme = useTheme();
  const rank = az.leader_preference ?? AZ_NOT_PREFERRED;

  if (isAzPreferred(az)) {
    return (
      <YBTag
        size="medium"
        customSx={{
          background: '#E8E9FE',
          color: theme.palette.grey[900],
          fontWeight: 400
        }}
        endIcon={
          rank === AZ_PREFFERED_HIGHEST_RANK ? (
            <PreferredRankStarIcon width={16} height={16} />
          ) : undefined
        }
      >
        {t('preferredYesRank', { rank })}
      </YBTag>
    );
  }

  return (
    <YBTag
      size="medium"
      customSx={{
        backgroundColor: theme.palette.grey[200],
        color: theme.palette.grey[700],
        fontWeight: 400
      }}
    >
      {t('notPreferred')}
    </YBTag>
  );
};

const PreferredColumnHeader: FC<{ onHelpClick: () => void }> = ({ onHelpClick }) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  return (
    <Box sx={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
      <span>{t('preferred')}</span>
      <Box
        component="span"
        data-testid="preferred-column-help"
        onClick={(e) => {
          e.preventDefault();
          e.stopPropagation();
          onHelpClick();
        }}
        sx={{ display: 'inline-flex', cursor: 'pointer', alignItems: 'center' }}
      >
        <HelpCircleIcon width={18} height={18} />
      </Box>
    </Box>
  );
};

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
  const [showPreferredInfoModal, setShowPreferredInfoModal] = useState(false);
  if (!universeData) return null;
  const universeUUID = universeData.info?.universe_uuid;
  const isK8s = placement.cloud_list?.[0]?.code === CloudType.kubernetes;
  const regionStats = countRegionsAzsAndNodes(placement);
  const resilientType = getResilientType(
    placement,
    parition?.replication_factor ?? cluster?.replication_factor,
    t
  );
  const isReadReplicaCluster = cluster.cluster_type === ClusterSpecClusterType.ASYNC;
  const dedicatedFromSpec = !!cluster.node_spec?.dedicated_nodes;
  const regionList = placement.cloud_list.map((cloud) => cloud.region_list).flat();
  const hasPreferredAz = regionList.some((region) => (region.az_list ?? []).some(isAzPreferred));

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

  const preferredColumn = {
    accessorKey: 'leader_affinity',
    header: t('preferred'),
    enableSorting: false,
    size: 160,
    // eslint-disable-next-line react/display-name
    Header: () => <PreferredColumnHeader onHelpClick={() => setShowPreferredInfoModal(true)} />,
    // eslint-disable-next-line react/display-name
    Cell: ({ row: azRow }: { row: { original: PlacementAZ } }) => (
      <PreferredAzTag az={azRow.original} />
    )
  };

  const renderExpandDetails = ({ row }: any) => {
    const placementRegion: PlacementRegion = row.original;
    const azList = (placementRegion.az_list ?? []) as unknown as Record<string, string>[];
    const columns: Array<Record<string, any>> = dedicatedFromSpec
      ? [
          { accessorKey: 'name', header: t('availabilityZone') },
          {
            accessorKey: 'tServers',
            header: t('tServers'),
            Cell: ({ row: azRow }: any) => azRow.original?.num_nodes_in_az ?? 0
          },
          ...(!isReadReplicaCluster
            ? [
                {
                  accessorKey: 'masters',
                  header: t('masters'),
                  Cell: ({ row: azRow }: any) =>
                    countNodesInAzByType(universeData!, azRow.original?.uuid, NodeDetailsDedicatedTo.MASTER)
                }
              ]
            : [])
        ]
      : [
          { accessorKey: 'name', header: t('availabilityZone') },
          { accessorKey: 'num_nodes_in_az', header: t(isK8s ? 'noOfPods' : 'noOfNodes') }
        ];

    if (hasPreferredAz) {
      columns.push(preferredColumn);
    }

    return (
      <NestedAzTable>
        <YBTable
          columns={columns}
          data={azList}
          withBorder={false}
          options={nestedAzTableOptions}
        />
      </NestedAzTable>
    );
  };

  return (
    <Box
      sx={{
        bgcolor: 'background.paper',
        mt: 2,
        padding: '10px 24px 24px 24px',
        borderRadius: 1,
        display: 'flex',
        flexDirection: 'column',
        width: '100%',
        gap: 2,
        border: '1px solid #E9EEF2'
      }}
    >
      <Box
        sx={{
          display: 'flex',
          flexDirection: 'row',
          justifyContent: 'space-between',
          alignItems: 'center',
          height: '64px',
          width: '100%'
        }}
      >
        <Typography sx={{ fontWeight: 600 }} variant="h5">
          {title}
        </Typography>
        <YBDropdown
          dataTestId="edit-placement-actions"
          disableScrollLock
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
              variant="ghost"
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
                <RbacValidator
                  accessRequiredOn={withUniverseResource(
                    ApiPermissionMap.EDIT_V2_UNIVERSE_PLACEMENT,
                    universeUUID
                  )}
                  isControl
                >
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
                </RbacValidator>,
                <RbacValidator
                  accessRequiredOn={withUniverseResource(
                    ApiPermissionMap.EDIT_V2_UNIVERSE_PLACEMENT,
                    universeUUID
                  )}
                  isControl
                >
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
                ...(editMasterServerNodeAllocationClicked && dedicatedFromSpec
                  ? [
                      <RbacValidator
                        accessRequiredOn={withUniverseResource(
                          ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER,
                          universeUUID
                        )}
                        isControl
                      >
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
            <Box sx={{ gap: '4px' }}>
              <Typography variant="button" color="textSecondary">
                {t('resilienceLevel')}
              </Typography>
              <StyledValue>{resilientType}</StyledValue>
            </Box>
            <Box sx={{ gap: '4px' }}>
              <Typography variant="button" color="textSecondary">
                {t('replicationFactor')}
              </Typography>
              <StyledValue>
                {parition?.replication_factor ?? cluster?.replication_factor ?? '-'}
              </StyledValue>
            </Box>
          </>
        )}
        <Box sx={{ gap: '4px' }}>
          <Typography variant="button" color="textSecondary">
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
        </Box>
      </StyledClusterInfo>
      <Box
        sx={{
          display: 'flex',
          flexDirection: 'column',
          width: '100%',
          padding: '0px 16px 8px 16px',
          alignSelf: 'strech'
        }}
      >
        <YBTable
          data={(regionList as unknown as Record<string, string>[]) ?? []}
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
              Cell: ({ cell }: any) => {
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
            renderDetailPanel: renderExpandDetails,
            pagination: false,
            initialState: {
              expanded: true
            },
            muiDetailPanelProps: regionTableDetailPanelProps
          }}
        />
      </Box>
      <PreferredInfoModal
        open={showPreferredInfoModal}
        onClose={() => {
          setShowPreferredInfoModal(false);
        }}
      />
    </Box>
  );
};
