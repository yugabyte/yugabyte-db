import { useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { toast } from 'react-toastify';
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
  shouldAutoIncludeIndexTables,
  getInConfigTableUuid
} from '../ReplicationUtils';
import { TableReplicationLagGraphModal } from './TableReplicationLagGraphModal';
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
  INPUT_FIELD_WIDTH_PX,
  liveMetricTimeRangeUnit,
  liveMetricTimeRangeValue,
  MetricName,
  XClusterModalName,
  XClusterTableStatus,
  XCLUSTER_UNDEFINED_LAG_NUMERIC_REPRESENTATION,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../constants';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { XClusterTableStatusLabel } from '../XClusterTableStatusLabel';
import { handleServerError } from '../../../utils/errorHandlingUtils';
import {
  getTableName,
  getIsTableInfoMissing,
  getIsXClusterReplicationTable
} from '../../../utils/tableUtils';
import { getAlertConfigurations } from '../../../actions/universe';

import {
  MetricsQueryParams,
  TableType,
  TableTypeLabel,
  YBTable
} from '../../../redesign/helpers/dtos';
import { XClusterReplicationTable } from '../XClusterTypes';
import { XClusterConfig } from '../dtos';
import { NodeAggregation, SplitType } from '../../metrics/dtos';
import { AlertTemplate } from '../../../redesign/features/alerts/TemplateComposer/ICustomVariables';
import {
  SearchToken,
  YBSmartSearchBar
} from '../../../redesign/components/YBSmartSearchBar/YBSmartSearchBar';
import {
  FieldType,
  isMatchedBySearchToken
} from '../../../redesign/components/YBSmartSearchBar/helpers';

import styles from './ReplicationTables.module.scss';

interface CommonReplicationTablesProps {
  xClusterConfig: XClusterConfig;
  isTableInfoIncludedInConfig: boolean;
  // isActive determines whether the component will make periodic
  // queries for metrics.
  isActive?: boolean;
}

type ReplicationTablesProps =
  | (CommonReplicationTablesProps & { isDrInterface: true; drConfigUuid: string })
  | (CommonReplicationTablesProps & { isDrInterface: false });

const TABLE_MIN_PAGE_SIZE = 10;

export function ReplicationTables(props: ReplicationTablesProps) {
  const { xClusterConfig, isTableInfoIncludedInConfig, isActive = true } = props;
  const [deleteTableDetails, setDeleteTableDetails] = useState<XClusterReplicationTable>();
  const [openTableLagGraphDetails, setOpenTableLagGraphDetails] = useState<
    XClusterReplicationTable
  >();
  const [searchTokens, setSearchTokens] = useState<SearchToken[]>([]);

  const dispatch = useDispatch();
  const { visibleModal } = useSelector((state: any) => state.modal);
  const queryClient = useQueryClient();
  const theme = useTheme();

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfig.sourceUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(
        xClusterConfig.sourceUniverseUUID,
        XCLUSTER_UNIVERSE_TABLE_FILTERS
      ).then((response) => response.data)
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
  const replicationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    xClusterConfigUuid: xClusterConfig.uuid,
    start: moment().subtract(liveMetricTimeRangeValue, liveMetricTimeRangeUnit).format('X'),
    end: moment().format('X')
  };
  const tableReplicationLagQuery = useQuery(
    metricQueryKey.live(
      replicationLagMetricRequestParams,
      liveMetricTimeRangeValue,
      liveMetricTimeRangeUnit
    ),
    () => api.fetchMetrics(replicationLagMetricRequestParams),
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
                  ? `"${getTableName(deleteTableDetails)}" table removed successfully.`
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

  const handleSearchTokenChange = (searchTokens: SearchToken[]) => {
    setSearchTokens(searchTokens);
  };

  const hideModal = () => {
    dispatch(closeDialog());
  };
  const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(alertConfigQuery.data);
  const xClusterTables = augmentTablesWithXClusterDetails(
    xClusterConfig.tableDetails,
    maxAcceptableLag,
    tableReplicationLagQuery.data?.async_replication_sent_lag?.data,
    isTableInfoIncludedInConfig,
    { includeUnconfiguredTables: true, includeDroppedTables: true }
  );
  const inConfigTableUuids = new Set<string>(getInConfigTableUuid(xClusterConfig.tableDetails));

  const sourceUniverse = sourceUniverseQuery.data;
  const filteredTablesInConfig = xClusterTables.filter((table) =>
    isTableMatchedBySearchTokens(table, searchTokens)
  );
  return (
    <div className={styles.rootContainer}>
      <YBSmartSearchBar
        searchTokens={searchTokens}
        onSearchTokensChange={handleSearchTokenChange}
        recognizedModifiers={[
          'database',
          'table',
          'schema',
          'sizeBytes',
          'status',
          'replicationLagMs'
        ]}
        placeholder="Search tables..."
        autoExpandMinWidth={INPUT_FIELD_WIDTH_PX}
      />
      <div className={styles.replicationTable}>
        <BootstrapTable
          data={filteredTablesInConfig}
          tableBodyClass={styles.table}
          trClassName="tr-row-style"
          pagination={xClusterTables && xClusterTables.length > TABLE_MIN_PAGE_SIZE}
        >
          <TableHeaderColumn dataField="tableUUID" isKey={true} hidden />
          <TableHeaderColumn
            dataField="tableName"
            dataFormat={(_, xClusterTable) => getTableName(xClusterTable)}
            dataSort
          >
            Table Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="pgSchemaName"
            dataFormat={(pgSchemaName: string, xClusterTable: YBTable | XClusterReplicationTable) =>
              getIsXClusterReplicationTable(xClusterTable) && getIsTableInfoMissing(xClusterTable)
                ? '-'
                : formatSchemaName(xClusterTable.tableType, pgSchemaName)
            }
            dataSort
          >
            Schema Name
          </TableHeaderColumn>
          {!props.isDrInterface && (
            <TableHeaderColumn
              dataField="tableType"
              dataFormat={(tableType: TableType) => TableTypeLabel[tableType]}
              dataSort
            >
              Table Type
            </TableHeaderColumn>
          )}
          <TableHeaderColumn
            dataField="keySpace"
            dataFormat={(keyspace) => keyspace ?? '-'}
            dataSort
          >
            Database
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="sizeBytes"
            dataFormat={(size) => formatBytes(size)}
            dataSort
          >
            Size
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="statusLabel"
            hidden={!isTableInfoIncludedInConfig}
            dataSort
            dataFormat={(_: XClusterTableStatus, xClusterTable: XClusterReplicationTable) => (
              <XClusterTableStatusLabel
                status={xClusterTable.status}
                errors={xClusterTable.replicationStatusErrors}
                isDrInterface={props.isDrInterface}
              />
            )}
          >
            Replication Status
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="replicationLag"
            dataFormat={(_, xClusterTable: XClusterReplicationTable) => {
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

              const formattedLag = formatLagMetric(xClusterTable.replicationLag);

              if (
                xClusterTable.replicationLag === undefined ||
                xClusterTable.replicationLag === XCLUSTER_UNDEFINED_LAG_NUMERIC_REPRESENTATION
              ) {
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
            dataSort
          >
            Current lag
          </TableHeaderColumn>
          <TableHeaderColumn
            columnClassName={styles.tableActionColumn}
            width="160px"
            dataField="action"
            dataFormat={(_, table: XClusterReplicationTable) => (
              <YBButton
                className={styles.actionButton}
                btnIcon="fa fa-line-chart"
                onClick={(e: any) => {
                  setOpenTableLagGraphDetails(table);
                  dispatch(openDialog(XClusterModalName.TABLE_REPLICATION_LAG_GRAPH));
                  e.currentTarget.blur();
                }}
              />
            )}
          ></TableHeaderColumn>
        </BootstrapTable>
      </div>
      {openTableLagGraphDetails && (
        <TableReplicationLagGraphModal
          xClusterTable={openTableLagGraphDetails}
          sourceUniverseUuid={sourceUniverse.universeUUID}
          nodePrefix={sourceUniverse.universeDetails.nodePrefix}
          queryEnabled={isActive}
          modalProps={{
            open: visibleModal === XClusterModalName.TABLE_REPLICATION_LAG_GRAPH,
            onClose: hideModal
          }}
        />
      )}
    </div>
  );
}

/**
 * Fields to do substring search on if search token modifier is not specified.
 */
const SUBSTRING_SEARCH_FIELDS = ['table', 'database', 'schema', 'status'];

const isTableMatchedBySearchTokens = (
  table: XClusterReplicationTable,
  searchTokens: SearchToken[]
) => {
  const candidate = {
    ...(!getIsTableInfoMissing(table) && {
      database: { value: table.keySpace, type: FieldType.STRING },
      schema: { value: table.pgSchemaName, type: FieldType.STRING },
      sizeBytes: { value: table.sizeBytes, type: FieldType.NUMBER },
      status: { value: table.statusLabel, type: FieldType.STRING },
      replicationLagMs: { value: table.replicationLag, type: FieldType.NUMBER }
    }),
    table: {
      value: getIsTableInfoMissing(table) ? table.tableUUID : table.tableName,
      type: FieldType.STRING
    }
  };
  return searchTokens.every((searchToken) =>
    isMatchedBySearchToken(candidate, searchToken, SUBSTRING_SEARCH_FIELDS)
  );
};
