import { useQuery } from 'react-query';
import moment from 'moment';

import { getAlertConfigurations } from '../../actions/universe';
import { fetchReplicationLag, isBootstrapRequired } from '../../actions/xClusterReplication';
import { formatLagMetric } from '../../utils/Formatters';
import {
  MetricName,
  MetricTraceName,
  XClusterConfigAction,
  XClusterConfigStatus,
  BROKEN_XCLUSTER_CONFIG_STATUSES,
  XClusterConfigType,
  XClusterTableEligibility,
  AlertName
} from './constants';
import {
  alertConfigQueryKey,
  api,
  metricQueryKey,
  universeQueryKey
} from '../../redesign/helpers/api';
import { getUniverseStatus } from '../universes/helpers/universeHelpers';
import { UnavailableUniverseStates, YBTableRelationType } from '../../redesign/helpers/constants';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';
import { SortOrder } from '../../redesign/helpers/constants';
import { getTableUuid } from '../../utils/tableUtils';

import {
  Metrics,
  MetricTimeRange,
  StandardMetricTimeRangeOption,
  XClusterTable,
  XClusterTableCandidate
} from './XClusterTypes';
import { XClusterConfig, XClusterTableDetails } from './dtos';
import { MetricTrace, TableType, Universe, YBTable } from '../../redesign/helpers/dtos';

import './ReplicationUtils.scss';

// TODO: Rename, refactor and pull into separate file
export const MaxAcceptableLag = ({
  currentUniverseUUID
}: {
  currentUniverseUUID: string | undefined;
}) => {
  const alertConfigFilter = {
    name: AlertName.REPLICATION_LAG,
    targetUuid: currentUniverseUUID
  };
  const maxAcceptableLagQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );

  if (maxAcceptableLagQuery.isLoading || maxAcceptableLagQuery.isIdle) {
    return <i className="fa fa-spinner fa-spin yb-spinner"></i>;
  }
  if (maxAcceptableLagQuery.isError || maxAcceptableLagQuery.data.length === 0) {
    return <span>-</span>;
  }

  const maxAcceptableLag = Math.min(
    ...maxAcceptableLagQuery.data.map(
      (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
    )
  );
  return <span>{formatLagMetric(maxAcceptableLag)}</span>;
};

// TODO: Rename, refactor and pull into separate file
export const CurrentReplicationLag = ({
  xClusterConfigUuid,
  xClusterConfigStatus,
  sourceUniverseUuid
}: {
  xClusterConfigUuid: string;
  xClusterConfigStatus: XClusterConfigStatus;
  sourceUniverseUuid: string | undefined;
}) => {
  const sourceUniverseQuery = useQuery(universeQueryKey.detail(sourceUniverseUuid), () =>
    api.fetchUniverse(sourceUniverseUuid)
  );

  const replciationLagMetricRequestParams = {
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    replicationUuid: xClusterConfigUuid
  };
  const universeLagQuery = useQuery(
    metricQueryKey.latest(replciationLagMetricRequestParams, '1', 'hour'),
    () => fetchReplicationLag(replciationLagMetricRequestParams),
    {
      enabled: !!sourceUniverseQuery.data
    }
  );

  const alertConfigFilter = {
    name: AlertName.REPLICATION_LAG,
    targetUuid: sourceUniverseUuid
  };
  const maxAcceptableLagQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );

  if (
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    universeLagQuery.isLoading ||
    universeLagQuery.isIdle ||
    maxAcceptableLagQuery.isLoading ||
    maxAcceptableLagQuery.isIdle
  ) {
    return <i className="fa fa-spinner fa-spin yb-spinner" />;
  }

  if (
    BROKEN_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfigStatus) ||
    sourceUniverseQuery.isError ||
    universeLagQuery.isError ||
    maxAcceptableLagQuery.isError
  ) {
    return <span>-</span>;
  }

  const maxAcceptableLag = Math.min(
    ...maxAcceptableLagQuery.data.map(
      (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
    )
  );

  const maxNodeLag = getLatestMaxNodeLag(universeLagQuery.data);
  const formattedLag = formatLagMetric(maxNodeLag);
  const isReplicationUnhealthy = maxNodeLag === undefined || maxNodeLag > maxAcceptableLag;

  if (maxNodeLag === undefined) {
    return <span className="replication-lag-value warning">{formattedLag}</span>;
  }

  return (
    <span
      className={`replication-lag-value ${
        isReplicationUnhealthy ? 'above-threshold' : 'below-threshold'
      }`}
    >
      {isReplicationUnhealthy && <i className="fa fa-exclamation-triangle" aria-hidden="true" />}
      {formattedLag}
    </span>
  );
};

// TODO: Rename, refactor and pull into separate file
export const CurrentTableReplicationLag = ({
  tableId,
  streamId,
  queryEnabled,
  nodePrefix,
  sourceUniverseUUID,
  xClusterConfigStatus
}: {
  tableId: string;
  streamId: string;
  queryEnabled: boolean;
  nodePrefix: string | undefined;
  sourceUniverseUUID: string | undefined;
  xClusterConfigStatus: XClusterConfigStatus;
}) => {
  const replciationLagMetricRequestParams = {
    nodePrefix,
    streamId,
    tableId
  };
  const tableLagQuery = useQuery(
    metricQueryKey.latest(replciationLagMetricRequestParams, '1', 'hour'),
    () => fetchReplicationLag(replciationLagMetricRequestParams),
    {
      enabled: queryEnabled
    }
  );

  const alertConfigFilter = {
    name: AlertName.REPLICATION_LAG,
    targetUuid: sourceUniverseUUID
  };
  const maxAcceptableLagQuery = useQuery(
    alertConfigQueryKey.list(alertConfigFilter),
    () => getAlertConfigurations(alertConfigFilter),
    {
      enabled: queryEnabled
    }
  );

  if (
    tableLagQuery.isLoading ||
    tableLagQuery.isIdle ||
    maxAcceptableLagQuery.isLoading ||
    maxAcceptableLagQuery.isIdle
  ) {
    return <i className="fa fa-spinner fa-spin yb-spinner" />;
  }

  if (
    BROKEN_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfigStatus) ||
    tableLagQuery.isError ||
    maxAcceptableLagQuery.isError
  ) {
    return <span>-</span>;
  }

  const maxAcceptableLag = Math.min(
    ...maxAcceptableLagQuery.data.map(
      (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
    )
  );

  const maxNodeLag = getLatestMaxNodeLag(tableLagQuery.data);
  const formattedLag = formatLagMetric(maxNodeLag);
  const isReplicationUnhealthy = maxNodeLag === undefined || maxNodeLag > maxAcceptableLag;

  if (maxNodeLag === undefined) {
    return <span className="replication-lag-value warning">{formattedLag}</span>;
  }

  return (
    <span
      className={`replication-lag-value ${
        isReplicationUnhealthy ? 'above-threshold' : 'below-threshold'
      }`}
    >
      {isReplicationUnhealthy && <i className="fa fa-exclamation-triangle" aria-hidden="true" />}
      {formattedLag}
    </span>
  );
};

export const getLatestMaxNodeLag = (metric: Metrics<'tserver_async_replication_lag_micros'>) => {
  const lagMetric = metric.tserver_async_replication_lag_micros;
  const traceAlias =
    lagMetric.layout.yaxis.alias[
      MetricTraceName[MetricName.TSERVER_ASYNC_REPLICATION_LAG].COMMITTED_LAG
    ];
  const traces = lagMetric.data.filter((trace) => trace.name === traceAlias);
  const latestLags = traces.reduce((latestLags: number[], trace) => {
    const latestLag = parseFloatIfDefined(trace.y[trace.y.length - 1]);
    if (latestLag !== undefined) {
      latestLags.push(latestLag);
    }
    return latestLags;
  }, []);
  return latestLags.length ? Math.max(...latestLags) : undefined;
};

export const getMaxNodeLagMetric = (
  metric: Metrics<'tserver_async_replication_lag_micros'>
): MetricTrace | undefined => {
  const lagMetric = metric.tserver_async_replication_lag_micros;
  const traceAlias =
    lagMetric.layout.yaxis.alias[
      MetricTraceName[MetricName.TSERVER_ASYNC_REPLICATION_LAG].COMMITTED_LAG
    ];
  const traces = lagMetric.data.filter((trace) => trace.name === traceAlias);
  if (!traces.length) {
    return undefined;
  }

  // Take the maximum y at every x across all nodes.
  const maxY = new Array<number>(traces[0].y.length).fill(0);
  traces.forEach((trace) => {
    trace.y.forEach((y: string | number, idx: number) => {
      maxY[idx] = Math.max(maxY[idx], parseFloatIfDefined(y) ?? 0);
    });
  });

  return {
    ...traces[0],
    name: `Max ${traceAlias}`,
    y: maxY
  };
};

// Improvement: Consider if we can do this data transform on the server.
export const adaptMetricDataForRecharts = (
  metricTraces: MetricTrace[],
  getUniqueTraceName: (trace: MetricTrace) => string
): { [dataKey: string]: number | null }[] => {
  const combinedDataPoints = new Map<number, { x: number; [dataKey: string]: number | null }>();
  metricTraces.forEach((trace) => {
    const traceName = getUniqueTraceName(trace);
    trace.x.forEach((x, index) => {
      // `null` values will be reflected as a gap in the chart unless we ask
      // Recharts to connect gaps.
      const y = parseFloatIfDefined(trace.y[index]) ?? null;
      const combinedY = combinedDataPoints.get(x);
      if (combinedY !== undefined) {
        combinedY[traceName] = y;
      } else {
        combinedDataPoints.set(x, { x, [traceName]: y });
      }
    });
  });
  return Array.from(combinedDataPoints.values()).sort(
    (dataPointA, dataPointB) => dataPointA.x - dataPointB.x
  );
};

export const getMasterNodeAddress = (nodeDetailsSet: Array<any>) => {
  const master = nodeDetailsSet.find((node: Record<string, any>) => node.isMaster);
  if (master) {
    return master.cloudInfo.private_ip + ':' + master.masterRpcPort;
  }
  return '';
};

export const convertToLocalTime = (time: string, timezone: string | undefined) => {
  return (timezone ? (moment.utc(time) as any).tz(timezone) : moment.utc(time).local()).format(
    'YYYY-MM-DD H:mm:ss'
  );
};

export const formatBytes = function (sizeInBytes: any) {
  if (Number.isInteger(sizeInBytes)) {
    const bytes = sizeInBytes;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB'];
    const k = 1024;
    if (bytes <= 0) {
      return bytes + ' ' + sizes[0];
    }

    const sizeIndex = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, sizeIndex)).toFixed(2)) + ' ' + sizes[sizeIndex];
  } else {
    return '-';
  }
};

/**
 * Returns an object containing the start and end moment for a given time range option.
 */
export const getMetricTimeRange = (
  metricTimeRangeOption: StandardMetricTimeRangeOption
): MetricTimeRange => ({
  startMoment: moment().subtract(metricTimeRangeOption.value, metricTimeRangeOption.type),
  endMoment: moment()
});

/**
 * Wraps parseFloat and lets undefined and number type values pass through.
 */
export const parseFloatIfDefined = (input: string | number | undefined) => {
  if (typeof input === 'number' || input === undefined) {
    return input;
  }
  return parseFloat(input);
};

export const getEnabledConfigActions = (
  replication: XClusterConfig,
  sourceUniverse: Universe | undefined,
  targetUniverse: Universe | undefined
): XClusterConfigAction[] => {
  if (
    UnavailableUniverseStates.includes(getUniverseStatus(sourceUniverse).state) ||
    UnavailableUniverseStates.includes(getUniverseStatus(targetUniverse).state)
  ) {
    // xCluster 'Delete' action will fail on the backend. But if the user selects the
    // 'force delete' option, then they will be able to remove the config even if a
    // participating universe is unavailable.
    return [XClusterConfigAction.DELETE];
  }

  switch (replication.status) {
    case XClusterConfigStatus.INITIALIZED:
    case XClusterConfigStatus.UPDATING:
      return [XClusterConfigAction.DELETE, XClusterConfigAction.RESTART];
    case XClusterConfigStatus.RUNNING:
      return [
        replication.paused ? XClusterConfigAction.RESUME : XClusterConfigAction.PAUSE,
        XClusterConfigAction.ADD_TABLE,
        XClusterConfigAction.MANAGE_TABLE,
        XClusterConfigAction.DB_SYNC,
        XClusterConfigAction.DELETE,
        XClusterConfigAction.EDIT,
        XClusterConfigAction.RESTART
      ];
    case XClusterConfigStatus.FAILED:
      return [XClusterConfigAction.DELETE, XClusterConfigAction.RESTART];
    case XClusterConfigStatus.DELETED_UNIVERSE:
    case XClusterConfigStatus.DELETION_FAILED:
      return [XClusterConfigAction.DELETE];
    default:
      return assertUnreachableCase(replication.status);
  }
};

/**
 * Returns the UUIDs for all xCluster configs associated with the provided universe.
 */
export const getXClusterConfigUuids = (universe: Universe | undefined) => ({
  sourceXClusterConfigUuids: universe?.universeDetails?.xclusterInfo?.sourceXClusterConfigs ?? [],
  targetXClusterConfigUuids: universe?.universeDetails?.xclusterInfo?.targetXClusterConfigs ?? []
});

export const hasLinkedXClusterConfig = (universes: Universe[]) =>
  universes.some((universe) => {
    const { sourceXClusterConfigUuids, targetXClusterConfigUuids } = getXClusterConfigUuids(
      universe
    );
    return sourceXClusterConfigUuids.length > 0 || targetXClusterConfigUuids.length > 0;
  });

/**
 * Returns the UUIDs for all xCluster configs with the provided source and target universe.
 */
export const getSharedXClusterConfigs = (sourceUniverse: Universe, targetUniverse: Universe) => {
  const sourceXClusterConfigs = sourceUniverse.universeDetails?.xclusterInfo?.sourceXClusterConfigs;
  const targetXClusterConfigs = targetUniverse.universeDetails?.xclusterInfo?.targetXClusterConfigs;

  const targetUniverseConfigUUIDs = new Set(targetXClusterConfigs);
  return sourceXClusterConfigs
    ? sourceXClusterConfigs.filter((configUUID) => targetUniverseConfigUUIDs.has(configUUID))
    : [];
};

/**
 * Adapt UUID to the format required for xCluster work.
 * - UUIDs are generally given in XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX format
 * - UUIDs used in xCluster endpoints often have the '-' stripped away
 */
export const formatUuidForXCluster = (tableUuid: string) => tableUuid.replaceAll('-', '');

/**
 * Adapt UUID from format required for xCluster work to the common format with dashes.
 * - UUIDs are generally given in XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX format
 * - UUIDs used in xCluster endpoints often have the '-' stripped away
 */
export const formatUuidFromXCluster = (tableUuid: string) =>
  tableUuid.replace(
    /^([0-9a-f]{8})([0-9a-f]{4})([0-9a-f]{4})([0-9a-f]{4})([0-9a-f]{12})$/,
    '$1-$2-$3-$4-$5'
  );

export const tableSort = <RowType,>(
  a: RowType,
  b: RowType,
  sortField: keyof RowType,
  sortOrder: SortOrder,
  tieBreakerField: keyof RowType
) => {
  let ord = 0;

  ord = a[sortField] < b[sortField] ? -1 : 1;
  // Break ties with the provided tiebreaker field in ascending order.
  if (a[sortField] === b[sortField]) {
    return a[tieBreakerField] < b[tieBreakerField] ? -1 : 1;
  }

  return sortOrder === SortOrder.ASCENDING ? ord : ord * -1;
};

/**
 * Return the `tableType` of any table in an xCluster config.
 */
export const getXClusterConfigTableType = (xClusterConfig: XClusterConfig) => {
  switch (xClusterConfig.tableType) {
    case 'YSQL':
      return TableType.PGSQL_TABLE_TYPE;
    case 'YCQL':
      return TableType.YQL_TABLE_TYPE;
    case 'UNKNOWN':
      return undefined;
  }
};

/**
 * Returns whether the provided table can be added/removed from the xCluster config.
 */
export const isTableToggleable = (
  table: XClusterTableCandidate,
  xClusterConfigAction: XClusterConfigAction
) =>
  table.eligibilityDetails.status === XClusterTableEligibility.ELIGIBLE_UNUSED ||
  (xClusterConfigAction === XClusterConfigAction.MANAGE_TABLE &&
    table.eligibilityDetails.status === XClusterTableEligibility.ELIGIBLE_IN_CURRENT_CONFIG);

/**
 * Returns array of XClusterTable by augmenting YBTable with XClusterTableDetails
 */
export const augmentTablesWithXClusterDetails = (
  ybTable: YBTable[],
  xClusterConfigTables: XClusterTableDetails[]
): XClusterTable[] => {
  const ybTableMap = new Map<string, YBTable>();
  ybTable.forEach((table) => {
    const { tableUUID, ...tableDetails } = table;
    const adaptedTableUUID = formatUuidForXCluster(getTableUuid(table));
    ybTableMap.set(adaptedTableUUID, { ...tableDetails, tableUUID: adaptedTableUUID });
  });
  const tables = xClusterConfigTables.reduce((tables: XClusterTable[], table) => {
    const ybTableDetails = ybTableMap.get(table.tableId);
    if (ybTableDetails) {
      const { tableId, ...xClusterTableDetails } = table;
      tables.push({ ...ybTableDetails, ...xClusterTableDetails });
    } else {
      console.error(
        `Missing table details for table ${table.tableId}. This table was found in an xCluster configuration but not in the corresponding source universe.`
      );
    }
    return tables;
  }, []);
  return tables;
};

/**
 * Return the UUIDs for tables which require bootstrapping.
 * May throw an error if the need_bootstrap query fails.
 */
export const getTablesForBootstrapping = async (
  selectedTableUuids: string[],
  sourceUniverseUuid: string,
  targetUniverseUuid: string | null,
  sourceUniverseTables: YBTable[],
  tableUuidsInConfig: string[],
  xClusterConfigType: XClusterConfigType,
  isUsedForDr: boolean
) => {
  // Check if bootstrap is required, for each selected table
  let bootstrapTest: { [tableUUID: string]: boolean } = {};

  bootstrapTest = await isBootstrapRequired(
    sourceUniverseUuid,
    targetUniverseUuid,
    selectedTableUuids.map(formatUuidForXCluster),
    xClusterConfigType
  );
  if (isUsedForDr) {
    // For DR, we always need to collect backup storage config for bootstrapping when the
    // user adds a new table to the config. The need_bootstrap endpoint will not return true for this
    // case. Thus, we need to set the test to true for these tables.
    const tableUuidsInConfigSet = new Set(tableUuidsInConfig);
    selectedTableUuids.forEach((tableUuid) => {
      if (!tableUuidsInConfigSet.has(tableUuid)) {
        bootstrapTest[tableUuid] = true;
      }
    });
  }

  const bootstrapRequiredTableUUIDs = new Set<string>();
  if (bootstrapTest) {
    const ysqlKeyspaceToTableUUIDs = new Map<string, Set<string>>();
    const ysqlTableUUIDToKeyspace = new Map<string, string>();
    sourceUniverseTables.forEach((table) => {
      if (
        table.tableType !== TableType.PGSQL_TABLE_TYPE ||
        table.relationType === YBTableRelationType.INDEX_TABLE_RELATION
      ) {
        // Ignore all index tables and non-YSQL tables.
        return;
      }
      // If a single YSQL table requires bootstrapping, then we must submit all table UUIDs
      // under the same database in the bootstrap param because the backup and restore is only available at a
      // database level.
      const tableUUIDs = ysqlKeyspaceToTableUUIDs.get(table.keySpace);
      if (tableUUIDs !== undefined) {
        tableUUIDs.add(formatUuidForXCluster(getTableUuid(table)));
      } else {
        ysqlKeyspaceToTableUUIDs.set(
          table.keySpace,
          new Set<string>([formatUuidForXCluster(getTableUuid(table))])
        );
      }
      ysqlTableUUIDToKeyspace.set(formatUuidForXCluster(getTableUuid(table)), table.keySpace);
    });

    Object.entries(bootstrapTest).forEach(([tableUUID, bootstrapRequired]) => {
      if (bootstrapRequired) {
        bootstrapRequiredTableUUIDs.add(tableUUID);
        // YSQL ONLY: In addition to the current table, add all other tables in the same keyspace
        //            for bootstrapping.
        const keyspace = ysqlTableUUIDToKeyspace.get(tableUUID);
        if (keyspace !== undefined) {
          const tableUUIDs = ysqlKeyspaceToTableUUIDs.get(keyspace);
          if (tableUUIDs !== undefined) {
            tableUUIDs.forEach((tableUUID) => bootstrapRequiredTableUUIDs.add(tableUUID));
          }
        }
      }
    });
  }

  return Array.from(bootstrapRequiredTableUUIDs);
};
