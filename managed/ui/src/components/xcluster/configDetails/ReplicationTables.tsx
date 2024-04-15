import { useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { AxiosError } from 'axios';
import moment from 'moment';
import { Box, useTheme } from '@material-ui/core';

import { closeDialog, openDialog } from '../../../actions/modal';
import {
  editXClusterConfigTables,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes
} from '../../../actions/xClusterReplication';
import { formatLagMetric, formatSchemaName } from '../../../utils/Formatters';
import { YBButton } from '../../common/forms/fields';
import {
  formatBytes,
  augmentTablesWithXClusterDetails,
  getStrictestReplicationLagAlertThreshold,
  shouldAutoIncludeIndexTables
} from '../ReplicationUtils';
import DeleteReplicactionTableModal from './DeleteReplicactionTableModal';
import { ReplicationLagGraphModal } from './ReplicationLagGraphModal';
import { YBLabelWithIcon } from '../../common/descriptors';
import ellipsisIcon from '../../common/media/more.svg';
import {
  alertConfigQueryKey,
  api,
  drConfigQueryKey,
  metricQueryKey,
  universeQueryKey,
  xClusterQueryKey
} from '../../../redesign/helpers/api';
import {
  BROKEN_XCLUSTER_CONFIG_STATUSES,
  liveMetricTimeRangeUnit,
  liveMetricTimeRangeValue,
  MetricName,
  XClusterConfigType,
  XClusterModalName,
  XClusterTableStatus,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../constants';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { XClusterTableStatusLabel } from '../XClusterTableStatusLabel';
import { handleServerError } from '../../../utils/errorHandlingUtils';
import {
  RbacValidator,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { getTableName } from '../../../utils/tableUtils';
import { getAlertConfigurations } from '../../../actions/universe';

import {
  MetricsQueryParams,
  TableType,
  TableTypeLabel,
  YBTable
} from '../../../redesign/helpers/dtos';
import { XClusterTable } from '../XClusterTypes';
import { XClusterConfig } from '../dtos';
import { NodeAggregation, SplitType } from '../../metrics/dtos';
import { AlertTemplate } from '../../../redesign/features/alerts/TemplateComposer/ICustomVariables';

import styles from './ReplicationTables.module.scss';

interface CommonReplicationTablesProps {
  xClusterConfig: XClusterConfig;

  // isActive determines whether the component will make periodic
  // queries for metrics.
  isActive?: boolean;
}

type ReplicationTablesProps =
  | (CommonReplicationTablesProps & { isDrInterface: true; drConfigUuid: string })
  | (CommonReplicationTablesProps & { isDrInterface: false });

const TABLE_MIN_PAGE_SIZE = 10;

export function ReplicationTables(props: ReplicationTablesProps) {
  const { xClusterConfig, isActive = true } = props;
  const [deleteTableDetails, setDeleteTableDetails] = useState<XClusterTable>();
  const [openTableLagGraphDetails, setOpenTableLagGraphDetails] = useState<XClusterTable>();

  const dispatch = useDispatch();
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);
  const queryClient = useQueryClient();
  const theme = useTheme();

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfig.sourceUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(
        xClusterConfig.sourceUniverseUUID,
        XCLUSTER_UNIVERSE_TABLE_FILTERS
      ).then((respone) => respone.data)
  );

  const sourceUniverseQuery = useQuery(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: xClusterConfig.sourceUniverseUUID
  };
  const alertConfigQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );

  const replicationLagMetricSettings = {
    metric: MetricName.ASYNC_REPLICATION_SENT_LAG,
    nodeAggregation: NodeAggregation.MAX,
    splitType: SplitType.TABLE
  };
  const replciationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    xClusterConfigUuid: xClusterConfig.uuid,
    start: moment().subtract(liveMetricTimeRangeValue, liveMetricTimeRangeUnit).format('X'),
    end: moment().format('X')
  };
  const tableReplicationLagQuery = useQuery(
    metricQueryKey.live(
      replciationLagMetricRequestParams,
      liveMetricTimeRangeValue,
      liveMetricTimeRangeUnit
    ),
    () => api.fetchMetrics(replciationLagMetricRequestParams),
    {
      enabled: !!sourceUniverseQuery.data
    }
  );

  const removeTableFromXCluster = useMutation(
    (replication: XClusterConfig) => {
      return props.isDrInterface
        ? api.updateTablesInDr(props.drConfigUuid, { tables: replication.tables })
        : editXClusterConfigTables(replication.uuid, {
            tables: replication.tables,
            autoIncludeIndexTables: shouldAutoIncludeIndexTables(xClusterConfig)
          });
    },
    {
      onSuccess: (response, xClusterConfig) => {
        fetchTaskUntilItCompletes(response.taskUUID, (err: boolean) => {
          if (!err) {
            queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
            if (props.isDrInterface) {
              queryClient.invalidateQueries(drConfigQueryKey.detail(props.drConfigUuid));
              toast.success(
                deleteTableDetails
                  ? `"${getTableName(deleteTableDetails)}" table removed successully.`
                  : 'Table removed successfully.'
              );
            } else {
              toast.success(
                deleteTableDetails
                  ? `"${getTableName(deleteTableDetails)}" table removed successfully from ${
                      xClusterConfig.name
                    }.`
                  : `Table removed successfully from ${xClusterConfig.name}`
              );
            }
            dispatch(closeDialog());
          } else {
            toast.error(
              <span className="alertMsg">
                <i className="fa fa-exclamation-circle" />
                <span>
                  {deleteTableDetails
                    ? `Failed to remove table "${getTableName(deleteTableDetails)}"${
                        props.isDrInterface ? '.' : ` from ${xClusterConfig.name}.`
                      }`
                    : `Failed to remove table${
                        props.isDrInterface ? '.' : ` from ${xClusterConfig.name}.`
                      }`}
                </span>
                <a href={`/tasks/${response.taskUUID}`} target="_blank" rel="noopener noreferrer">
                  View Details
                </a>
              </span>
            );
          }
        });
      },
      onError: (error: Error | AxiosError) => {
        handleServerError(error, { customErrorLabel: 'Remove table request failed' });
      }
    }
  );

  if (
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }
  if (sourceUniverseTablesQuery.isError || sourceUniverseQuery.isError) {
    const sourceUniverseTerm = props.isDrInterface ? 'DR primary universe' : 'source universe';
    const errorMessage = sourceUniverseTablesQuery.isError
      ? `Failed to fetch ${sourceUniverseTerm} table details.`
      : `Failed to fetch ${sourceUniverseTerm} details.`;
    return <YBErrorIndicator customErrorMessage={errorMessage} />;
  }

  const hideModal = () => {
    dispatch(closeDialog());
  };

  const tablesInConfig = augmentTablesWithXClusterDetails(
    sourceUniverseTablesQuery.data,
    xClusterConfig.tableDetails,
    tableReplicationLagQuery.data?.async_replication_sent_lag?.data
  );
  const sourceUniverse = sourceUniverseQuery.data;
  return (
    <div className={styles.rootContainer}>
      {!props.isDrInterface && (
        // TODO: Ask Yu-Shen if we can remove this line so both xCluster and xCluster DR are consistent.
        <div className={styles.headerSection}>
          <span className={styles.infoText}>Tables selected for Replication</span>
        </div>
      )}
      <div className={styles.replicationTable}>
        <BootstrapTable
          data={tablesInConfig}
          tableBodyClass={styles.table}
          trClassName="tr-row-style"
          pagination={tablesInConfig && tablesInConfig.length > TABLE_MIN_PAGE_SIZE}
        >
          <TableHeaderColumn dataField="tableUUID" isKey={true} hidden />
          <TableHeaderColumn
            dataField="tableName"
            dataFormat={(_, xClusterTable) => getTableName(xClusterTable)}
          >
            Table Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="pgSchemaName"
            dataFormat={(cell: string, row: YBTable) => formatSchemaName(row.tableType, cell)}
          >
            Schema Name
          </TableHeaderColumn>
          {!props.isDrInterface && (
            <TableHeaderColumn
              dataField="tableType"
              dataFormat={(cell: TableType) => TableTypeLabel[cell]}
            >
              Table Type
            </TableHeaderColumn>
          )}
          <TableHeaderColumn dataField="keySpace">Database</TableHeaderColumn>
          <TableHeaderColumn dataField="sizeBytes" dataFormat={(cell) => formatBytes(cell)}>
            Size
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="status"
            dataFormat={(cell: XClusterTableStatus, xClusterTable: XClusterTable) => (
              <XClusterTableStatusLabel
                replicationLag={xClusterTable.replicationLag}
                status={cell}
                sourceUniverseUuid={sourceUniverse.universeUUID}
              />
            )}
          >
            Replication Status
          </TableHeaderColumn>
          <TableHeaderColumn
            dataFormat={(_, xClusterTable: XClusterTable) => {
              if (
                tableReplicationLagQuery.isLoading ||
                tableReplicationLagQuery.isIdle ||
                alertConfigQuery.isLoading ||
                alertConfigQuery.isIdle
              ) {
                return <i className="fa fa-spinner fa-spin yb-spinner" />;
              }

              if (
                BROKEN_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfig.status) ||
                tableReplicationLagQuery.isError ||
                alertConfigQuery.isError
              ) {
                return <span>-</span>;
              }

              const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(
                alertConfigQuery.data
              );
              const formattedLag = formatLagMetric(xClusterTable.replicationLag);

              if (xClusterTable.replicationLag === undefined) {
                return <span className="replication-lag-value warning">{formattedLag}</span>;
              }

              const isReplicationUnhealthy =
                maxAcceptableLag && xClusterTable.replicationLag > maxAcceptableLag;

              return (
                <Box
                  className={`replication-lag-value ${
                    isReplicationUnhealthy ? 'above-threshold' : 'below-threshold'
                  }`}
                  display="flex"
                  alignItems="center"
                  gridGap={theme.spacing(1)}
                >
                  {isReplicationUnhealthy && (
                    <i className="fa fa-exclamation-triangle" aria-hidden="true" />
                  )}
                  {formattedLag}
                </Box>
              );
            }}
          >
            Current lag
          </TableHeaderColumn>
          <TableHeaderColumn
            columnClassName={styles.tableActionColumn}
            width="160px"
            dataField="action"
            dataFormat={(_, row: XClusterTable) => (
              <>
                <YBButton
                  className={styles.actionButton}
                  btnIcon="fa fa-line-chart"
                  onClick={(e: any) => {
                    setOpenTableLagGraphDetails(row);
                    dispatch(openDialog(XClusterModalName.TABLE_REPLICATION_LAG_GRAPH));
                    e.currentTarget.blur();
                  }}
                />
                <Dropdown id={`${styles.tableActionColumn}_dropdown`} pullRight>
                  <Dropdown.Toggle noCaret className={styles.actionButton}>
                    <img src={ellipsisIcon} alt="more" className="ellipsis-icon" />
                  </Dropdown.Toggle>
                  <Dropdown.Menu>
                    <RbacValidator
                      customValidateFunction={() => {
                        return (
                          hasNecessaryPerm({
                            ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                            onResource: xClusterConfig.sourceUniverseUUID
                          }) &&
                          hasNecessaryPerm({
                            ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                            onResource: xClusterConfig.targetUniverseUUID
                          })
                        );
                      }}
                      isControl
                    >
                      <MenuItem
                        onClick={() => {
                          setDeleteTableDetails(row);
                          dispatch(openDialog(XClusterModalName.REMOVE_TABLE_FROM_CONFIG));
                        }}
                        disabled={row.tableType === TableType.TRANSACTION_STATUS_TABLE_TYPE}
                      >
                        <YBLabelWithIcon className={styles.dropdownMenuItem} icon="fa fa-times">
                          Remove Table
                        </YBLabelWithIcon>
                      </MenuItem>
                    </RbacValidator>
                  </Dropdown.Menu>
                </Dropdown>
              </>
            )}
          ></TableHeaderColumn>
        </BootstrapTable>
      </div>
      {openTableLagGraphDetails && (
        <ReplicationLagGraphModal
          tableDetails={openTableLagGraphDetails}
          replicationUUID={xClusterConfig.uuid}
          universeUUID={sourceUniverse.universeUUID}
          nodePrefix={sourceUniverse.universeDetails.nodePrefix}
          queryEnabled={isActive}
          visible={visibleModal === XClusterModalName.TABLE_REPLICATION_LAG_GRAPH}
          onHide={hideModal}
        />
      )}
      <DeleteReplicactionTableModal
        deleteTableName={deleteTableDetails ? getTableName(deleteTableDetails) : ''}
        onConfirm={() => {
          removeTableFromXCluster.mutate({
            ...xClusterConfig,
            tables: xClusterConfig.tables.filter((t) => t !== deleteTableDetails?.tableUUID)
          });
        }}
        onCancel={hideModal}
      />
    </div>
  );
}
