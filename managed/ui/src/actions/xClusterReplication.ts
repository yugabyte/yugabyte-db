import axios from 'axios';
import moment from 'moment';

import { ROOT_URL } from '../config';
import { Metrics } from '../components/xcluster';
import { getCustomerEndpoint } from './common';
import {
  MetricName,
  XClusterConfigState,
  XClusterConfigType
} from '../components/xcluster/constants';
import { ApiTimeout } from '../redesign/helpers/api';
import { YBPTask } from '../redesign/helpers/dtos';
import { XClusterConfig } from '../components/xcluster/dtos';

// TODO: Move this out of the /actions folder since these functions aren't Redux actions.

export function getUniverseInfo(universeUUID: string) {
  const cUUID = localStorage.getItem('customerId');
  return axios.get(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}`);
}

/**
 * @deprecated Pleaes use fetchUniverseList from helpers/api.ts instead.
 */
export function fetchUniversesList() {
  const cUUID = localStorage.getItem('customerId');
  return axios.get(`${ROOT_URL}/customers/${cUUID}/universes`);
}

export type UniverseTableFilters = {
  excludeColocatedTables?: boolean;
  includeParentTableInfo?: boolean;
  xClusterSupportedOnly?: boolean;
};
export function fetchTablesInUniverse(
  universeUUID: string | undefined,
  filters?: UniverseTableFilters
) {
  if (universeUUID) {
    const customerId = localStorage.getItem('customerId');
    return axios.get(`${ROOT_URL}/customers/${customerId}/universes/${universeUUID}/tables`, {
      params: filters,
      timeout: ApiTimeout.FETCH_TABLE_INFO
    });
  }
  return Promise.reject('Querying universe tables failed: No universe UUID provided.');
}

export function createXClusterReplication(
  targetUniverseUUID: string,
  sourceUniverseUUID: string,
  name: string,
  configType: XClusterConfigType,
  tables: string[],
  bootstrapParams?: {
    tables: string[];
    backupRequestParams: any;
  }
) {
  const customerId = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${customerId}/xcluster_configs`, {
    sourceUniverseUUID,
    targetUniverseUUID,
    name,
    configType,
    tables,
    ...(bootstrapParams !== undefined && { bootstrapParams })
  });
}

export function restartXClusterConfig(
  xClusterUUID: string,
  tables: string[],
  bootstrapParams: { backupRequestParams: any }
) {
  const customerId = localStorage.getItem('customerId');
  return axios
    .post<YBPTask>(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${xClusterUUID}`, {
      tables,
      bootstrapParams
    })
    .then((response) => response.data);
}

export function isBootstrapRequired(
  sourceUniverseUUID: string,
  targetUniverseUUID: string | null,
  tableUUIDs: string[],
  configType: XClusterConfigType = XClusterConfigType.BASIC
) {
  const customerId = localStorage.getItem('customerId');
  return axios
    .post<{ [tableUUID: string]: boolean }>(
      `${ROOT_URL}/customers/${customerId}/universes/${sourceUniverseUUID}/need_bootstrap`,
      {
        tables: tableUUIDs,
        targetUniverseUUID
      },
      { params: { configType } }
    )
    .then((response) => response.data);
}

export function isCatchUpBootstrapRequired(
  xClusterConfigUUID: string | undefined,
  tableUUIDs: string[] | undefined
) {
  const customerId = localStorage.getItem('customerId');
  if (tableUUIDs && xClusterConfigUUID) {
    return axios
      .post<{ [tableUUID: string]: boolean }>(
        `${ROOT_URL}/customers/${customerId}/xcluster_configs/${xClusterConfigUUID}/need_bootstrap`,
        { tables: tableUUIDs }
      )
      .then((response) => response.data);
  }
  const errorMsg = xClusterConfigUUID ? 'No table UUIDs provided' : 'No xCluster UUID provided';
  return Promise.reject(`Querying bootstrap requirement failed: ${errorMsg}.`);
}

export function fetchXClusterConfig(xClusterConfigUUID: string) {
  const customerId = localStorage.getItem('customerId');
  return axios
    .get<XClusterConfig>(
      `${ROOT_URL}/customers/${customerId}/xcluster_configs/${xClusterConfigUUID}`
    )
    .then((response) => response.data);
}

export function editXClusterState(xClusterConfigUUID: string, state: XClusterConfigState) {
  const customerId = localStorage.getItem('customerId');
  return axios.put(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${xClusterConfigUUID}`, {
    status: state
  });
}

export function editXclusterName(replication: XClusterConfig) {
  const customerId = localStorage.getItem('customerId');
  return axios.put(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${replication.uuid}`, {
    name: replication.name
  });
}

export function editXClusterConfigTables(
  xClusterUUID: string,
  tables: string[],
  bootstrapParams?: {
    tables: string[];
    backupRequestParams: any;
  }
) {
  const customerId = localStorage.getItem('customerId');
  return axios
    .put<YBPTask>(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${xClusterUUID}`, {
      tables: tables,
      ...(bootstrapParams !== undefined && { bootstrapParams })
    })
    .then((response) => response.data);
}

export function deleteXclusterConfig(uuid: string, isForceDelete: boolean) {
  const customerId = localStorage.getItem('customerId');
  return axios.delete(
    `${ROOT_URL}/customers/${customerId}/xcluster_configs/${uuid}?isForceDelete=${isForceDelete}`
  );
}

/**
 * Set the provided xCluster config to whatever is on the database.
 *
 * Context:
 * Users can interact with an xCluster config using the YBA API or yb-admin.
 * The purpose of the sync API is to reconcile changes to an xCluster config as a
 * result of yb-admin commands.
 */
export function syncXClusterConfigWithDB(replicationGroupName: string, targetUniverseUUID: string) {
  const customerUUID = localStorage.getItem('customerId');
  return axios
    .post<YBPTask>(`${ROOT_URL}/customers/${customerUUID}/xcluster_configs/sync`, {
      replicationGroupName: replicationGroupName,
      targetUniverseUUID: targetUniverseUUID
    })
    .then((response) => response.data);
}

export function queryLagMetricsForUniverse(
  nodePrefix: string | undefined,
  replicationUUID: string
) {
  const DEFAULT_GRAPH_FILTER = {
    start: moment().utc().subtract('1', 'hour').format('X'),
    end: moment().utc().format('X'),
    nodePrefix,
    metrics: [MetricName.TSERVER_ASYNC_REPLICATION_LAG_METRIC],
    xClusterConfigUuid: replicationUUID
  };

  const customerUUID = localStorage.getItem('customerId');
  return axios
    .post<Metrics<'tserver_async_replication_lag_micros'>>(
      `${ROOT_URL}/customers/${customerUUID}/metrics`,
      DEFAULT_GRAPH_FILTER
    )
    .then((response) => response.data);
}

export function queryLagMetricsForTable(
  streamId: string,
  tableId: string,
  nodePrefix: string | undefined,
  start = moment().utc().subtract('1', 'hour').format('X'),
  end = moment().utc().format('X')
) {
  const DEFAULT_GRAPH_FILTER = {
    start,
    end,
    streamId,
    tableId,
    nodePrefix,
    metrics: [MetricName.TSERVER_ASYNC_REPLICATION_LAG_METRIC]
  };
  const customerUUID = localStorage.getItem('customerId');
  return axios
    .post<Metrics<'tserver_async_replication_lag_micros'>>(
      `${ROOT_URL}/customers/${customerUUID}/metrics`,
      DEFAULT_GRAPH_FILTER
    )
    .then((response) => response.data);
}

export function fetchUniverseDiskUsageMetric(
  nodePrefix: string,
  start = moment().utc().subtract('1', 'hour').format('X'),
  end = moment().utc().format('X')
) {
  const graphFilter = {
    start,
    end,
    nodePrefix,
    metrics: [MetricName.DISK_USAGE]
  };
  const customerUUID = localStorage.getItem('customerId');
  return axios
    .post<Metrics<'disk_usage'>>(`${ROOT_URL}/customers/${customerUUID}/metrics`, graphFilter)
    .then((response) => response.data);
}

export function fetchTaskProgress(taskUUID: string) {
  return axios.get(`${getCustomerEndpoint()}/tasks/${taskUUID}`);
}

const DEFAULT_TASK_REFETCH_INTERVAL = 1000;
type callbackFunc = (err: boolean, data: any) => void;

export function fetchTaskUntilItCompletes(
  taskUUID: string,
  onTaskCompletion: callbackFunc,
  onTaskStarted?: () => void,
  interval = DEFAULT_TASK_REFETCH_INTERVAL
) {
  let taskRunning = false;
  async function retryTask() {
    try {
      const resp = await fetchTaskProgress(taskUUID);
      const { percent, status } = resp.data;
      if (percent > 0 && taskRunning === false) {
        onTaskStarted && onTaskStarted();
        taskRunning = true;
      }
      if (status === 'Failed' || status === 'Failure') {
        onTaskCompletion(true, resp);
      } else if (percent === 100) {
        onTaskCompletion(false, resp.data);
      } else {
        setTimeout(retryTask, interval);
      }
      // eslint-disable-next-line no-empty
    } catch {}
  }
  return retryTask();
}
