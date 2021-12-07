import React from 'react';
import { useQuery } from 'react-query';
import { getAlertConfigurations } from '../../actions/universe';
import {
  getUniverseInfo,
  queryLagMetricsForTable,
  queryLagMetricsForUniverse
} from '../../actions/xClusterReplication';
import { IReplicationStatus } from './IClusterReplication';

import './ReplicationUtils.scss';

export const YSQL_TABLE_TYPE = 'PGSQL_TABLE_TYPE';

export const getReplicationStatus = (status = IReplicationStatus.INIT) => {
  switch (status) {
    case IReplicationStatus.SUCCESS:
    case IReplicationStatus.RUNNING:
      return (
        <span className="replication-status-text success">
          <i className="fa fa-check" />
          Enabled
        </span>
      );
    case IReplicationStatus.PAUSED:
      return (
        <span className="replication-status-text paused">
          <i className="fa fa-pause-circle-o" />
          Paused
        </span>
      );
    case IReplicationStatus.INIT:
      return (
        <span className="replication-status-text success">
          <i className="fa fa-info" />
          Init
        </span>
      );
    case IReplicationStatus.FAILED:
      return (
        <span className="replication-status-text failed">
          <i className="fa fa-info-circle" />
          Failed
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

const ALERT_NAME = 'Replication Lag';
export const GetConfiguredThreshold = ({
  currentUniverseUUID
}: {
  currentUniverseUUID: string;
}) => {
  const configurationFilter = {
    name: ALERT_NAME,
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
  return <span>{metricsData?.[0]?.thresholds?.SEVERE.threshold}</span>;
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
    [replicationUUID, nodePrefix, 'metric'],
    () => queryLagMetricsForUniverse(nodePrefix),
    {
      enabled: !currentUniverseLoading,
      refetchInterval: 20 * 1000
    }
  );

  if (isFetching || currentUniverseLoading) {
    return <i className="fa fa-spinner fa-spin yb-spinner"></i>;
  }

  if (
    !metricsData?.data.tserver_async_replication_lag_micros ||
    !Array.isArray(metricsData.data.tserver_async_replication_lag_micros.data)
  ) {
    return <span>-</span>;
  }

  const latestLag = metricsData.data.tserver_async_replication_lag_micros.data[1]?.y[0];
  return <span>{latestLag || '-'}</span>;
};

export const GetCurrentLagForTable = ({
  replicationUUID,
  tableName,
  enabled,
  nodePrefix
}: {
  replicationUUID: string;
  tableName: string;
  enabled?: boolean;
  nodePrefix: string | undefined;
}) => {
  const { data: metricsData, isFetching } = useQuery(
    [replicationUUID, nodePrefix, tableName, 'metric'],
    () => queryLagMetricsForTable(tableName, nodePrefix),
    {
      enabled,
      refetchInterval: 20 * 1000
    }
  );

  if (isFetching) {
    return <i className="fa fa-spinner fa-spin yb-spinner"></i>;
  }

  if (
    !metricsData?.data.tserver_async_replication_lag_micros ||
    !Array.isArray(metricsData.data.tserver_async_replication_lag_micros.data)
  ) {
    return <span>-</span>;
  }

  const latestLag = metricsData.data.tserver_async_replication_lag_micros.data[1]?.y[0];
  return <span>{latestLag || '-'}</span>;
};

export const getMasterNodeAddress = (nodeDetailsSet: Array<any>) => {
  const master = nodeDetailsSet.find((node: Record<string, any>) => node.isMaster);
  if (master) {
    return master.cloudInfo.private_ip + ':' + master.masterRpcPort;
  }
  return '';
};
