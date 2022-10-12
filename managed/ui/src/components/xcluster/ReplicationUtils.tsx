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
  ReplicationAction,
  ReplicationStatus,
  REPLICATION_LAG_ALERT_NAME,
  SortOrder
} from './constants';
import { api } from '../../redesign/helpers/api';

import { XClusterConfig } from './XClusterTypes';
import { Universe } from '../../redesign/helpers/dtos';

import './ReplicationUtils.scss';

export const YSQL_TABLE_TYPE = 'PGSQL_TABLE_TYPE';

export const GetConfiguredThreshold = ({
  currentUniverseUUID
}: {
  currentUniverseUUID: string;
}) => {
  const configurationFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: currentUniverseUUID
  };
  const { data: metricsData, isFetching } = useQuery(
    ['getConfiguredThreshold', configurationFilter],
    () => getAlertConfigurations(configurationFilter)
  );
  if (isFetching) {
    return <i className="fa fa-spinner fa-spin yb-spinner"></i>;
  }

  if (!metricsData) {
    return <span>0</span>;
  }
  const maxAcceptableLag = metricsData?.[0]?.thresholds?.SEVERE.threshold;
  return <span>{formatLagMetric(maxAcceptableLag)}</span>;
};

export const GetCurrentLag = ({
  replicationUUID,
  sourceUniverseUUID
}: {
  replicationUUID: string;
  sourceUniverseUUID: string;
}) => {
  const { data: universeInfo, isLoading: currentUniverseLoading } = useQuery(
    ['universe', sourceUniverseUUID],
    () => api.fetchUniverse(sourceUniverseUUID)
  );
  const nodePrefix = universeInfo?.universeDetails.nodePrefix;

  const { data: metricsData, isFetching } = useQuery(
    ['xcluster-metric', replicationUUID, nodePrefix, 'metric'],
    () => queryLagMetricsForUniverse(nodePrefix, replicationUUID),
    {
      enabled: !currentUniverseLoading
    }
  );
  const configurationFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: sourceUniverseUUID
  };
  const { data: configuredThreshold, isLoading: threshholdLoading } = useQuery(
    ['getConfiguredThreshold', configurationFilter],
    () => getAlertConfigurations(configurationFilter)
  );

  if (isFetching || currentUniverseLoading || threshholdLoading) {
    return <i className="fa fa-spinner fa-spin yb-spinner"></i>;
  }

  if (
    !metricsData?.data.tserver_async_replication_lag_micros ||
    !Array.isArray(metricsData.data.tserver_async_replication_lag_micros.data)
  ) {
    return <span>-</span>;
  }
  const maxAcceptableLag = configuredThreshold?.[0]?.thresholds?.SEVERE.threshold || 0;

  const metricAliases = metricsData.data.tserver_async_replication_lag_micros.layout.yaxis.alias;
  const committedLagName = metricAliases['async_replication_committed_lag_micros'];

  const latestLag = Math.max(
    ...metricsData.data.tserver_async_replication_lag_micros.data
      .filter((d: any) => d.name === committedLagName)
      .map((a: any) => {
        return a.y.slice(-1);
      })
  );
  const formattedLag = formatLagMetric(latestLag);
  const isReplicationUnhealthy = latestLag > maxAcceptableLag;

  return (
    <span
      className={`replication-lag-value ${
        isReplicationUnhealthy ? 'above-threshold' : 'below-threshold'
      }`}
    >
      {isReplicationUnhealthy && <i className="fa fa-exclamation-triangle" aria-hidden="true" />}
      {formattedLag ?? '-'}
    </span>
  );
};

export const GetCurrentLagForTable = ({
  replicationUUID,
  tableUUID,
  enabled,
  nodePrefix,
  sourceUniverseUUID
}: {
  replicationUUID: string;
  tableUUID: string;
  enabled?: boolean;
  nodePrefix: string | undefined;
  sourceUniverseUUID: string;
}) => {
  const { data: metricsData, isFetching } = useQuery(
    ['xcluster-metric', replicationUUID, nodePrefix, tableUUID, 'metric'],
    () => queryLagMetricsForTable(tableUUID, nodePrefix),
    {
      enabled
    }
  );

  const configurationFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: sourceUniverseUUID
  };
  const { data: configuredThreshold, isLoading: thresholdLoading } = useQuery(
    ['getConfiguredThreshold', configurationFilter],
    () => getAlertConfigurations(configurationFilter)
  );

  if (isFetching || thresholdLoading) {
    return <i className="fa fa-spinner fa-spin yb-spinner"></i>;
  }

  if (
    !metricsData?.data.tserver_async_replication_lag_micros ||
    !Array.isArray(metricsData.data.tserver_async_replication_lag_micros.data)
  ) {
    return <span>-</span>;
  }

  const maxAcceptableLag = configuredThreshold?.[0]?.thresholds?.SEVERE.threshold || 0;

  const metricAliases = metricsData.data.tserver_async_replication_lag_micros.layout.yaxis.alias;
  const committedLagName = metricAliases['async_replication_committed_lag_micros'];

  const latestLag = Math.max(
    ...metricsData.data.tserver_async_replication_lag_micros.data
      .filter((d: any) => d.name === committedLagName)
      .map((a: any) => {
        return a.y.slice(-1);
      })
  );
  const formattedLag = formatLagMetric(latestLag);

  return (
    <span
      className={`replication-lag-value ${
        maxAcceptableLag < latestLag ? 'above-threshold' : 'below-threshold'
      }`}
    >
      {formattedLag ?? '-'}
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

export const findUniverseName = function (universeList: Array<any>, universeUUID: string): string {
  return universeList.find((universe: any) => universe.universeUUID === universeUUID)?.name;
};

export const getEnabledConfigActions = (replication: XClusterConfig): ReplicationAction[] => {
  switch (replication.status) {
    case ReplicationStatus.INITIALIZED:
    case ReplicationStatus.UPDATING:
      return [ReplicationAction.DELETE, ReplicationAction.RESTART];
    case ReplicationStatus.RUNNING:
      return [
        replication.paused ? ReplicationAction.RESUME : ReplicationAction.PAUSE,
        ReplicationAction.DELETE,
        ReplicationAction.EDIT,
        ReplicationAction.ADD_TABLE,
        ReplicationAction.RESTART
      ];
    case ReplicationStatus.FAILED:
      return [ReplicationAction.DELETE, ReplicationAction.RESTART];
    case ReplicationStatus.DELETED_UNIVERSE:
    case ReplicationStatus.DELETION_FAILED:
      return [ReplicationAction.DELETE];
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

