import React from 'react';
import { useQuery } from 'react-query';
import moment from 'moment';

import { getAlertConfigurations } from '../../actions/universe';
import {
  queryLagMetricsForTable,
  queryLagMetricsForUniverse
} from '../../actions/xClusterReplication';
import { formatLagMetric } from '../../utils/Formatters';
import {
  MetricName,
  MetricTraceName,
  XClusterConfigAction,
  ReplicationStatus,
  REPLICATION_LAG_ALERT_NAME,
  SortOrder
} from './constants';
import { api } from '../../redesign/helpers/api';
import { assertUnreachableCase } from '../../utils/ErrorUtils';

import { XClusterConfig, XClusterTable, XClusterTableDetails } from './XClusterTypes';
import { Universe, YBTable } from '../../redesign/helpers/dtos';

import './ReplicationUtils.scss';

export const YSQL_TABLE_TYPE = 'PGSQL_TABLE_TYPE';

const COMMITTED_LAG_METRIC_TRACE_NAME =
  MetricTraceName[MetricName.TSERVER_ASYNC_REPLICATION_LAG_METRIC].COMMITTED_LAG;

// TODO: Rename, refactor and pull into separate file
export const MaxAcceptableLag = ({ currentUniverseUUID }: { currentUniverseUUID: string }) => {
  const alertConfigFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: currentUniverseUUID
  };
  const maxAcceptableLagQuery = useQuery(['alert', 'configurations', alertConfigFilter], () =>
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
  replicationUUID,
  sourceUniverseUUID
}: {
  replicationUUID: string;
  sourceUniverseUUID: string;
}) => {
  const currentUniverseQuery = useQuery(['universe', sourceUniverseUUID], () =>
    api.fetchUniverse(sourceUniverseUUID)
  );
  const universeLagQuery = useQuery(
    [
      'xcluster-metric',
      replicationUUID,
      currentUniverseQuery.data?.universeDetails.nodePrefix,
      'metric'
    ],
    () =>
      queryLagMetricsForUniverse(
        currentUniverseQuery.data?.universeDetails.nodePrefix,
        replicationUUID
      ),
    {
      enabled: !!currentUniverseQuery.data
    }
  );

  const alertConfigFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: sourceUniverseUUID
  };
  const maxAcceptableLagQuery = useQuery(['alert', 'configurations', alertConfigFilter], () =>
    getAlertConfigurations(alertConfigFilter)
  );

  if (
    currentUniverseQuery.isLoading ||
    currentUniverseQuery.isIdle ||
    universeLagQuery.isLoading ||
    universeLagQuery.isIdle ||
    maxAcceptableLagQuery.isLoading ||
    maxAcceptableLagQuery.isIdle
  ) {
    return <i className="fa fa-spinner fa-spin yb-spinner" />;
  }

  if (currentUniverseQuery.error || universeLagQuery.isError || maxAcceptableLagQuery.isError) {
    return <span>-</span>;
  }

  const maxAcceptableLag = Math.min(
    ...maxAcceptableLagQuery.data.map(
      (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
    )
  );

  const metric = universeLagQuery.data.tserver_async_replication_lag_micros;
  const traceAlias = metric.layout.yaxis.alias[COMMITTED_LAG_METRIC_TRACE_NAME];
  const trace = metric.data.find((trace) => (trace.name = traceAlias));
  const latestLag = parseFloatIfDefined(trace?.y[trace.y.length - 1]);
  const formattedLag = formatLagMetric(latestLag);
  const isReplicationUnhealthy = latestLag === undefined || latestLag > maxAcceptableLag;

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
  tableUUID,
  queryEnabled,
  nodePrefix,
  sourceUniverseUUID
}: {
  tableUUID: string;
  queryEnabled: boolean;
  nodePrefix: string | undefined;
  sourceUniverseUUID: string;
}) => {
  const tableLagQuery = useQuery(
    ['xcluster-metric', nodePrefix, tableUUID, 'metric'],
    () => queryLagMetricsForTable(tableUUID, nodePrefix),
    {
      enabled: queryEnabled
    }
  );

  const alertConfigFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: sourceUniverseUUID
  };
  const maxAcceptableLagQuery = useQuery(
    ['alert', 'configurations', alertConfigFilter],
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

  if (tableLagQuery.isError || maxAcceptableLagQuery.isError) {
    return <span>-</span>;
  }

  const maxAcceptableLag = Math.min(
    ...maxAcceptableLagQuery.data.map(
      (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
    )
  );
  const metric = tableLagQuery.data.tserver_async_replication_lag_micros;
  const traceAlias = metric.layout.yaxis.alias[COMMITTED_LAG_METRIC_TRACE_NAME];
  const trace = metric.data.find((trace) => trace.name === traceAlias);
  const latestLag = parseFloatIfDefined(trace?.y[trace.y.length - 1]);
  const formattedLag = formatLagMetric(latestLag);
  const isReplicationUnhealthy = latestLag === undefined || latestLag > maxAcceptableLag;

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
 * Wraps parseFloat and lets undefined and number type values pass through.
 */
export const parseFloatIfDefined = (input: string | number | undefined) => {
  if (typeof input === 'number' || input === undefined) {
    return input;
  }
  return parseFloat(input);
};

export const findUniverseName = function (universeList: Array<any>, universeUUID: string): string {
  return universeList.find((universe: any) => universe.universeUUID === universeUUID)?.name;
};

export const getUniverseByUUID = (universeList: Universe[], uuid: string) => {
  return universeList.find((universes) => universes.universeUUID === uuid);
};

export const getEnabledConfigActions = (replication: XClusterConfig): XClusterConfigAction[] => {
  switch (replication.status) {
    case ReplicationStatus.INITIALIZED:
    case ReplicationStatus.UPDATING:
      return [XClusterConfigAction.DELETE, XClusterConfigAction.RESTART];
    case ReplicationStatus.RUNNING:
      return [
        replication.paused ? XClusterConfigAction.RESUME : XClusterConfigAction.PAUSE,
        XClusterConfigAction.DELETE,
        XClusterConfigAction.EDIT,
        XClusterConfigAction.ADD_TABLE,
        XClusterConfigAction.RESTART
      ];
    case ReplicationStatus.FAILED:
      return [XClusterConfigAction.DELETE, XClusterConfigAction.RESTART];
    case ReplicationStatus.DELETED_UNIVERSE:
    case ReplicationStatus.DELETION_FAILED:
      return [XClusterConfigAction.DELETE];
    default:
      return assertUnreachableCase(replication.status);
  }
};

/**
 * Returns the UUID for all xCluster configs with the provided source and target universe.
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
 * Adapt tableUUID to the format required for xCluster work.
 * - tableUUID is given in XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX format from
 *   /customers/<customerUUID>/universes/<universeUUID>/tables endpoint
 * - tableUUID used in xCluster endpoints have the '-' stripped away
 */
export const adaptTableUUID = (tableUUID: string) => tableUUID.replaceAll('-', '');

export const tableSort = <RowType,>(
  a: RowType,
  b: RowType,
  sortField: keyof RowType,
  sortOrder: SortOrder,
  tieBreakerField: keyof RowType
) => {
  let ord = 0;

  ord = a[sortField] < b[sortField] ? -1 : 1;
  // Break ties with the provided tie breaker field in ascending order.
  if (a[sortField] === b[sortField]) {
    return a[tieBreakerField] < b[tieBreakerField] ? -1 : 1;
  }

  return sortOrder === SortOrder.ASCENDING ? ord : ord * -1;
};

// TODO:
// Investigate whether we can store table type as a property of xCluster config.
// This will help reduce complexity and avoid filtering through all source universe tables.
// JIRA: https://yugabyte.atlassian.net/browse/PLAT-6095
/**
 * - Return the `tableType` of any table in an xCluster config.
 *   - The underlying assumption is that tables within an xCluster config should all have the same `tableType`.
 * - Returns undefined if no source universe tables exist in the xCluster config (Error/Unexpected case)
 */
export const getXClusterConfigTableType = (
  xClusterConfig: XClusterConfig,
  sourceUniverseTables: YBTable[]
) =>
  sourceUniverseTables.find((table) =>
    xClusterConfig.tables.includes(adaptTableUUID(table.tableUUID))
  )?.tableType;

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
    const adaptedTableUUID = adaptTableUUID(table.tableUUID);
    ybTableMap.set(adaptedTableUUID, { ...tableDetails, tableUUID: adaptedTableUUID });
  });
  return xClusterConfigTables.reduce((tables: XClusterTable[], table) => {
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
};
