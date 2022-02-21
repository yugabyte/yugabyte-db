import React from 'react';
import { useQuery } from 'react-query';
import { getAlertConfigurations } from '../../actions/universe';
import {
  getUniverseInfo,
  queryLagMetricsForTable,
  queryLagMetricsForUniverse
} from '../../actions/xClusterReplication';
import { IReplicationStatus } from './IClusterReplication';
import moment from 'moment';

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
    const configurationFilter = {
      name: ALERT_NAME,
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
  let maxAcceptableLag = configuredThreshold?.[0]?.thresholds?.SEVERE.threshold || 0;

  const latestLag = metricsData.data.tserver_async_replication_lag_micros.data[0]?.y.pop();
  return <span className={`replication-lag-value ${maxAcceptableLag < latestLag ? 'above-threshold' : 'below-threshold'}`}>{latestLag || '-'}</span>;
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

export const convertToLocalTime = (time:string, timezone:string) => {
  return (timezone ?  (moment.utc(time) as any).tz(timezone): moment.utc(time).local()).format('YYYY-MM-DD H:mm:ss')
}

export const formatBytes = function (sizeInBytes:any) {
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