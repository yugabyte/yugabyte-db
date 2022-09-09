import React from 'react';
import { useQuery } from 'react-query';
import moment from 'moment';

import { getAlertConfigurations } from '../../actions/universe';
import {
  getUniverseInfo,
  queryLagMetricsForTable,
  queryLagMetricsForUniverse
} from '../../actions/xClusterReplication';
import { formatLagMetric } from '../../utils/Formatters';
import { Replication } from './XClusterReplicationTypes';
import { ReplicationStatus, REPLICATION_LAG_ALERT_NAME } from './constants';

import './ReplicationUtils.scss';

export const YSQL_TABLE_TYPE = 'PGSQL_TABLE_TYPE';

export const getReplicationStatus = (replication: Replication) => {
  switch (replication.status) {
    case ReplicationStatus.UPDATING:
      return (
        <span className="replication-status-text updating">
          <i className="fa fa-spinner fa-spin" />
          Updating
        </span>
      );
    case ReplicationStatus.RUNNING:
      return replication.paused ? (
        <span className="replication-status-text paused">
          <i className="fa fa-pause-circle-o" />
          Paused
        </span>
      ) : (
        <span className="replication-status-text success">
          <i className="fa fa-check" />
          Enabled
        </span>
      );
    case ReplicationStatus.Initialized:
      return (
        <span className="replication-status-text success">
          <i className="fa fa-info" />
          Initialized
        </span>
      );
    case ReplicationStatus.FAILED:
      return (
        <span className="replication-status-text failed">
          <i className="fa fa-info-circle" />
          Failed
        </span>
      );
    case ReplicationStatus.DELETION_FAILED:
      return (
        <span className="replication-status-text failed">
          <i className="fa fa-close" />
          Deleted
        </span>
      );
    case ReplicationStatus.DELETED_UNIVERSE:
      return (
        <span className="replication-status-text failed">
          <i className="fa fa-close" />
          {replication.sourceUniverseUUID === undefined
            ? 'Source universe is deleted'
            : replication.targetUniverseUUID === undefined
            ? 'Target universe is deleted'
            : 'One participating universe was tried to be destroyed'}
        </span>
      );
    default:
      return (
        <span className="replication-status-text failed">
          <i className="fa fa-close" />
          Not Enabled
        </span>
      );
  }
};

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
    () => getUniverseInfo(sourceUniverseUUID)
  );
  const nodePrefix = universeInfo?.data?.universeDetails.nodePrefix;

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

export const convertToLocalTime = (time: string, timezone: string) => {
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

export const findUniverseName = function (universeList: Array<any>, universeUUID: string) {
  return universeList.find((universe: any) => universe.universeUUID === universeUUID)?.name;
};

export const isChangeDisabled = function (status: ReplicationStatus | undefined) {
  // Allow the operation for an unknown situation to avoid bugs.
  if (status === undefined) {
    return true;
  }
  return (
    status === ReplicationStatus.Initialized ||
    status === ReplicationStatus.UPDATING ||
    status === ReplicationStatus.DELETION_FAILED ||
    status === ReplicationStatus.DELETED_UNIVERSE
  );
};
