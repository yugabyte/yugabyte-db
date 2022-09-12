import axios from 'axios';
import moment from 'moment';

import { ROOT_URL } from '../config';
import { Replication, TableReplicationMetric } from '../components/xcluster';
import { getCustomerEndpoint } from './common';
import { XClusterConfigState } from '../components/xcluster/constants';

export function getUniverseInfo(universeUUID: string) {
  const cUUID = localStorage.getItem('customerId');
  return axios.get(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}`);
}

export function fetchUniversesList() {
  const cUUID = localStorage.getItem('customerId');
  return axios.get(`${ROOT_URL}/customers/${cUUID}/universes`);
}

export function fetchTablesInUniverse(universeUUID: string) {
  const customerId = localStorage.getItem('customerId');
  return axios.get(`${ROOT_URL}/customers/${customerId}/universes/${universeUUID}/tables`);
}

export function createXClusterReplication(
  targetUniverseUUID: string,
  sourceUniverseUUID: string,
  name: string,
  tables: string[]
) {
  const customerId = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${customerId}/xcluster_configs`, {
    sourceUniverseUUID,
    targetUniverseUUID,
    name,
    tables
  });
}

export function getXclusterConfig(uuid: string) {
  const customerId = localStorage.getItem('customerId');
  return axios
    .get<Replication>(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${uuid}`)
    .then((resp) => resp.data);
}

export function editXClusterState(replication: Replication, state: XClusterConfigState) {
  const customerId = localStorage.getItem('customerId');
  return axios.put(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${replication.uuid}`, {
    status: state
  });
}

export function editXclusterName(replication: Replication) {
  const customerId = localStorage.getItem('customerId');
  return axios.put(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${replication.uuid}`, {
    name: replication.name
  });
}

export function editXClusterTables(replication: Replication) {
  const customerId = localStorage.getItem('customerId');
  return axios.put(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${replication.uuid}`, {
    tables: replication.tables
  });
}

export function deleteXclusterConfig(uuid: string) {
  const customerId = localStorage.getItem('customerId');
  return axios.delete(`${ROOT_URL}/customers/${customerId}/xcluster_configs/${uuid}`);
}

export function queryLagMetricsForUniverse(
  nodePrefix: string | undefined,
  replicationUUID: string
) {
  const DEFAULT_GRAPH_FILTER = {
    start: moment().utc().subtract('1', 'hour').format('X'),
    end: moment().utc().format('X'),
    nodePrefix,
    metrics: ['tserver_async_replication_lag_micros'],
    xClusterConfigUuid: replicationUUID
  };

  const customerUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${customerUUID}/metrics`, DEFAULT_GRAPH_FILTER);
}

export function queryLagMetricsForTable(
  tableId: string,
  nodePrefix: string | undefined,
  start = moment().utc().subtract('1', 'hour').format('X'),
  end = moment().utc().format('X')
) {
  const DEFAULT_GRAPH_FILTER = {
    start,
    end,
    tableId,
    nodePrefix,
    metrics: ['tserver_async_replication_lag_micros']
  };
  const customerUUID = localStorage.getItem('customerId');
  return axios.post<TableReplicationMetric>(
    `${ROOT_URL}/customers/${customerUUID}/metrics`,
    DEFAULT_GRAPH_FILTER
  );
}

export function fetchTaskProgress(taskUUID: string) {
  return axios.get(`${getCustomerEndpoint()}/tasks/${taskUUID}`);
}

const DEFAULT_TASK_REFETCH_INTERVAL = 1000;
type callbackFunc = (err: boolean, data: any) => void;

export function fetchTaskUntilItCompletes(
  taskUUID: string,
  callback: callbackFunc,
  interval = DEFAULT_TASK_REFETCH_INTERVAL
) {
  async function retryTask() {
    try {
      const resp = await fetchTaskProgress(taskUUID);
      const { percent, status } = resp.data;
      if (status === 'Failed' || status === 'Failure') {
        callback(true, resp);
      } else if (percent === 100) {
        callback(false, resp.data);
      } else {
        setTimeout(retryTask, interval);
      }
    } catch {}
  }
  return retryTask();
}
