// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';
import Cookies from 'js-cookie';
import { getCustomerEndpoint } from './common';

// Create Universe
export const CREATE_UNIVERSE = 'CREATE_NEW_UNIVERSE';
export const CREATE_UNIVERSE_RESPONSE = 'CREATE_UNIVERSE_RESPONSE';

// Edit Universe
export const EDIT_UNIVERSE = 'EDIT_UNIVERSE';
export const EDIT_UNIVERSE_RESPONSE = 'EDIT_UNIVERSE_RESPONSE';

// Get Universe
export const FETCH_UNIVERSE_INFO = 'FETCH_UNIVERSE_INFO';
export const FETCH_UNIVERSE_INFO_SUCCESS = 'FETCH_UNIVERSE_INFO_SUCCESS';
export const FETCH_UNIVERSE_INFO_FAILURE = 'FETCH_UNIVERSE_INFO_FAILURE';
export const RESET_UNIVERSE_INFO = 'RESET_UNIVERSE_INFO';
export const FETCH_UNIVERSE_INFO_RESPONSE = 'FETCH_UNIVERSE_INFO_RESPONSE';

// Get List Of Universe
export const FETCH_UNIVERSE_LIST = 'FETCH_UNIVERSE_LIST';
export const FETCH_UNIVERSE_LIST_RESPONSE = 'FETCH_UNIVERSE_LIST_RESPONSE';
export const FETCH_UNIVERSE_LIST_SUCCESS = 'FETCH_UNIVERSE_LIST_SUCCESS';
export const FETCH_UNIVERSE_LIST_FAILURE = 'FETCH_UNIVERSE_LIST_FAILURE';
export const RESET_UNIVERSE_LIST = 'RESET_UNIVERSE_LIST';

// Delete Universe
export const DELETE_UNIVERSE = 'DELETE_UNIVERSE';
export const DELETE_UNIVERSE_RESPONSE = 'DELETE_UNIVERSE_RESPONSE';

// Pause Universe
export const PAUSE_UNIVERSE = 'PAUSE_UNIVERSE';
export const PAUSE_UNIVERSE_RESPONSE = 'PAUSE_UNIVERSE_RESPONSE';

// Restart Universe
export const RESTART_UNIVERSE = 'RESTART_UNIVERSE';
export const RESTART_UNIVERSE_RESPONSE = 'RESTART_UNIVERSE_RESPONSE';

// Get Universe load balancer state
export const FETCH_UNIVERSE_LB_STATE = 'FETCH_UNIVERSE_LB_STATE';
export const FETCH_UNIVERSE_LB_STATE_RESPONSE = 'FETCH_UNIVERSE_LB_STATE_RESPONSE';

// Read replicas
export const ADD_READ_REPLICA = 'ADD_READ_REPLICA';
export const ADD_READ_REPLICA_RESPONSE = 'ADD_READ_REPLICA_RESPONSE';
export const EDIT_READ_REPLICA = 'EDIT_READ_REPLICA';
export const EDIT_READ_REPLICA_RESPONSE = 'EDIT_READ_REPLICA_RESPONSE';
export const DELETE_READ_REPLICA = 'DELETE_READ_REPLICA';
export const DELETE_READ_REPLICA_RESPONSE = 'DELETE_READ_REPLICA_RESPONSE';

// Get the Master Leader for a Universe
export const GET_MASTER_INFO = 'GET_MASTER_INFO';
export const GET_MASTER_INFO_RESPONSE = 'GET_MASTER_INFO_RESPONSE';

// Get the Master Nodes Info for a Universe
export const GET_MASTER_LEADER = 'GET_MASTER_LEADER';
export const GET_MASTER_LEADER_RESPONSE = 'GET_MASTER_LEADER_RESPONSE';
export const RESET_MASTER_LEADER = 'RESET_MASTER_LEADER';

// Commissioner Tasks for Universe
export const FETCH_UNIVERSE_TASKS = 'FETCH_UNIVERSE_TASKS';
export const FETCH_UNIVERSE_TASKS_RESPONSE = 'FETCH_UNIVERSE_TASKS_RESPONSE';
export const RESET_UNIVERSE_TASKS = 'RESET_UNIVERSE_TASKS';

// Universe Co-Modal Tasks
export const CLOSE_UNIVERSE_DIALOG = 'CLOSE_UNIVERSE_DIALOG';

// Submit G-Flag Tasks
export const ROLLING_UPGRADE = 'ROLLING_UPGRADE';
export const ROLLING_UPGRADE_RESPONSE = 'ROLLING_UPGRADE_RESPONSE';
export const RESET_ROLLING_UPGRADE = 'RESET_ROLLING_UPGRADE';

// Universe Template Tasks
export const CONFIGURE_UNIVERSE_TEMPLATE = 'CONFIGURE_UNIVERSE_TEMPLATE';
export const CONFIGURE_UNIVERSE_TEMPLATE_RESPONSE = 'CONFIGURE_UNIVERSE_TEMPLATE_RESPONSE';
export const CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS = 'CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS';
export const CONFIGURE_UNIVERSE_TEMPLATE_LOADING = 'CONFIGURE_UNIVERSE_TEMPLATE_LOADING';

export const CONFIGURE_UNIVERSE_RESOURCES = 'CONFIGURE_UNIVERSE_RESOURCES';
export const CONFIGURE_UNIVERSE_RESOURCES_RESPONSE = 'CONFIGURE_UNIVERSE_RESOURCES_RESPONSE';

// Universe per-node status
export const GET_UNIVERSE_PER_NODE_STATUS = 'GET_UNIVERSE_PER_NODE_STATUS';
export const GET_UNIVERSE_PER_NODE_STATUS_RESPONSE = 'GET_UNIVERSE_PER_NODE_STATUS_RESPONSE';
export const GET_UNIVERSE_PER_NODE_METRICS = 'GET_UNIVERSE_PER_NODE_METRICS';
export const GET_UNIVERSE_PER_NODE_METRICS_RESPONSE = 'GET_UNIVERSE_PER_NODE_METRICS_RESPONSE';

// Node Actions
export const GET_NODE_DETAILS = 'GET_NODE_DETAILS';
export const GET_NODE_DETAILS_RESPONSE = 'GET_NODE_DETAILS_RESPONSE';
export const RESET_NODE_DETAILS = 'RESET_NODE_DETAILS';

//Validation Tasks
export const CHECK_IF_UNIVERSE_EXISTS = 'CHECK_IF_UNIVERSE_EXISTS';

// Set Universe Read Write Metrics
export const SET_UNIVERSE_METRICS = 'SET_UNIVERSE_METRICS';

// Set Placement Status
export const SET_PLACEMENT_STATUS = 'SET_PLACEMENT_STATUS';

// Reset Universe Configuration
export const RESET_UNIVERSE_CONFIGURATION = 'RESET_UNIVERSE_CONFIGURATION';

export const FETCH_UNIVERSE_METADATA = 'FETCH_UNIVERSE_METADATA';

export const PERFORM_UNIVERSE_NODE_ACTION = 'PERFORM_UNIVERSE_NODE_ACTION';
export const PERFORM_UNIVERSE_NODE_ACTION_RESPONSE = 'PERFORM_UNIVERSE_NODE_ACTION_RESPONSE';

export const FETCH_UNIVERSE_BACKUPS = 'FETCH_UNIVERSE_BACKUPS';
export const FETCH_UNIVERSE_BACKUPS_RESPONSE = 'FETCH_UNIVERSE_BACKUPS_RESPONSE';
export const RESET_UNIVERSE_BACKUPS = 'RESET_UNIVERSE_BACKUPS';

export const CREATE_UNIVERSE_BACKUP = 'CREATE_UNIVERSE_BACKUP';
export const CREATE_UNIVERSE_BACKUP_RESPONSE = 'CREATE_UNIVERSE_BACKUP_RESPONSE';

// Health Checking for Universe
export const GET_HEALTH_CHECK = 'GET_HEALTH_CHECK';
export const GET_HEALTH_CHECK_RESPONSE = 'GET_HEALTH_CHECK_RESPONSE';

export const SET_ENCRYPTION_KEY = 'SET_ENCRYPTION_KEY';
export const SET_ENCRYPTION_KEY_RESPONSE = 'SET_ENCRYPTION_KEY_RESPONSE';

export const UPDATE_BACKUP_STATE = 'UPDATE_BACKUP_STATE';
export const UPDATE_BACKUP_STATE_RESPONSE = 'UPDATE_BACKUP_STATE_RESPONSE';

export const FETCH_SUPPORTED_RELEASES = 'FETCH_SUPPORTED_RELEASES';
export const FETCH_SUPPORTED_RELEASES_RESPONSE = 'FETCH_SUPPORTED_RELEASES_RESPONSE';

/**
 *  Mapping from taskType to api route
 * */
const UPGRADE_TASKS = {
  VMImage: 'vm'
};

export function createUniverse(formValues) {
  const customerUUID = localStorage.getItem('customerId');
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/universes`, formValues);
  return {
    type: CREATE_UNIVERSE,
    payload: request
  };
}

export function createUniverseResponse(response) {
  return {
    type: CREATE_UNIVERSE_RESPONSE,
    payload: response
  };
}

export function fetchUniverseInfo(universeUUID) {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}`);
  return {
    type: FETCH_UNIVERSE_INFO,
    payload: request
  };
}

export function fetchUniverseInfoSuccess(universeInfo) {
  return {
    type: FETCH_UNIVERSE_INFO_SUCCESS,
    payload: universeInfo
  };
}

export function fetchUniverseInfoFailure(error) {
  return {
    type: FETCH_UNIVERSE_INFO_FAILURE,
    payload: error
  };
}

export function resetUniverseInfo() {
  return {
    type: RESET_UNIVERSE_INFO
  };
}

export function fetchUniverseInfoResponse(response) {
  return {
    type: FETCH_UNIVERSE_INFO_RESPONSE,
    payload: response
  };
}

export function fetchUniverseLbState(universeUUID) {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.get(
    `${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/master_lb_state`
  );
  return {
    type: FETCH_UNIVERSE_LB_STATE,
    payload: request
  };
}

export function fetchUniverseLbStateResponse(response) {
  return {
    type: FETCH_UNIVERSE_LB_STATE_RESPONSE,
    payload: response
  };
}

export function fetchReleasesByProvider(pUUID) {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/providers/${pUUID}/releases`);
  return {
    type: FETCH_SUPPORTED_RELEASES,
    payload: request
  };
}

export function fetchReleasesResponse(response) {
  return {
    type: FETCH_SUPPORTED_RELEASES_RESPONSE,
    payload: response
  };
}

export function fetchUniverseList() {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/universes`);

  return {
    type: FETCH_UNIVERSE_LIST,
    payload: request
  };
}

export function fetchUniverseListResponse(response) {
  return {
    type: FETCH_UNIVERSE_LIST_RESPONSE,
    payload: response
  };
}

export function fetchUniverseListSuccess(universeList) {
  return {
    type: FETCH_UNIVERSE_LIST_SUCCESS,
    payload: universeList
  };
}

export function fetchUniverseListFailure(error) {
  return {
    type: FETCH_UNIVERSE_LIST_FAILURE,
    payload: error
  };
}

export function resetUniverseList() {
  return {
    type: RESET_UNIVERSE_LIST
  };
}

export function deleteUniverse(universeUUID, isForceDelete, isDeleteBackups) {
  const customerUUID = localStorage.getItem('customerId');
  const deleteRequestPayload = {
    isForceDelete,
    isDeleteBackups
  };
  const request = axios.delete(`${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}`, {
    params: deleteRequestPayload
  });
  return {
    type: DELETE_UNIVERSE,
    payload: request
  };
}

export function deleteUniverseResponse(response) {
  return {
    type: DELETE_UNIVERSE_RESPONSE,
    payload: response
  };
}

export function pauseUniverse(universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const request = axios.post(
    `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/pause`
  );
  return {
    type: PAUSE_UNIVERSE,
    payload: request
  };
}

export function pauseUniverseResponse(response) {
  return {
    type: PAUSE_UNIVERSE_RESPONSE,
    payload: response
  };
}

export function restartUniverse(universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const request = axios.post(
    `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/resume`
  );
  return {
    type: RESTART_UNIVERSE,
    payload: request
  };
}

export function restartUniverseResponse(response) {
  return {
    type: RESTART_UNIVERSE_RESPONSE,
    payload: response
  };
}

export function editUniverse(formValues, universeUUID) {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.put(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}`, formValues);
  return {
    type: EDIT_UNIVERSE,
    payload: request
  };
}

export function editUniverseResponse(response) {
  return {
    type: EDIT_UNIVERSE_RESPONSE,
    payload: response
  };
}

export function addUniverseReadReplica(formValues, universeUUID) {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.post(
    `${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/cluster`,
    formValues
  );
  return {
    type: ADD_READ_REPLICA,
    payload: request
  };
}

export function addUniverseReadReplicaResponse(response) {
  return {
    type: ADD_READ_REPLICA_RESPONSE,
    payload: response
  };
}

export function editUniverseReadReplica(formValues, universeUUID) {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.put(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}`, formValues);
  return {
    type: EDIT_READ_REPLICA,
    payload: request
  };
}

export function editUniverseReadReplicaResponse(response) {
  return {
    type: EDIT_READ_REPLICA_RESPONSE,
    payload: response
  };
}

export function deleteUniverseReadReplica(clusterUUID, universeUUID, isForceDelete) {
  const cUUID = localStorage.getItem('customerId');
  const deleteRequestPayload = { isForceDelete: isForceDelete };
  const request = axios.delete(
    `${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/cluster/${clusterUUID}`,
    { params: deleteRequestPayload }
  );
  return {
    type: DELETE_READ_REPLICA,
    payload: request
  };
}

export function deleteUniverseReadReplicaResponse(response) {
  return {
    type: DELETE_READ_REPLICA_RESPONSE,
    payload: response
  };
}

export function fetchUniverseTasks(universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const request = axios.get(
    `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/tasks`
  );
  return {
    type: FETCH_UNIVERSE_TASKS,
    payload: request
  };
}

export function fetchUniverseTasksResponse(response) {
  return {
    type: FETCH_UNIVERSE_TASKS_RESPONSE,
    payload: response
  };
}

export function resetUniverseTasks() {
  return {
    type: RESET_UNIVERSE_TASKS
  };
}

export function closeUniverseDialog() {
  return {
    type: CLOSE_UNIVERSE_DIALOG
  };
}

export function rollingUpgrade(values, universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const taskEndPoint = values.taskType.toLowerCase();

  let request;
  if (values.taskType === 'Certs') {
    // This is to enable cert rotation for kubernetes universes
    // For kubernetes universes we fallback to old modal
    // But as we need to call the update_tls API we update the request accordingly
    request = axios.post(
      `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/update_tls`,
      values
    );
  } else {
    request = axios.post(
      `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/upgrade/${
        UPGRADE_TASKS[values.taskType] ?? taskEndPoint
      }`,
      values
    );
  }
  delete values.taskType;

  return {
    type: ROLLING_UPGRADE,
    payload: request
  };
}

export function rollingUpgradeResponse(response) {
  return {
    type: ROLLING_UPGRADE_RESPONSE,
    payload: response
  };
}

export function configureUniverseTemplateLoading() {
  return {
    type: CONFIGURE_UNIVERSE_TEMPLATE_LOADING
  };
}

export function configureUniverseTemplate(values) {
  const customerUUID = localStorage.getItem('customerId');
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/universe_configure`, values);
  return {
    type: CONFIGURE_UNIVERSE_TEMPLATE,
    payload: request
  };
}

export function configureUniverseTemplateResponse(response) {
  return {
    type: CONFIGURE_UNIVERSE_TEMPLATE_RESPONSE,
    payload: response
  };
}

export function configureUniverseTemplateSuccess(result) {
  return {
    type: CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS,
    payload: result
  };
}

export function configureUniverseResources(values) {
  const customerUUID = localStorage.getItem('customerId');
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/universe_resources`, values);
  return {
    type: CONFIGURE_UNIVERSE_RESOURCES,
    payload: request
  };
}

export function configureUniverseResourcesResponse(response) {
  return {
    type: CONFIGURE_UNIVERSE_RESOURCES_RESPONSE,
    payload: response
  };
}

export function getUniversePerNodeStatus(universeUUID) {
  const requestUrl = `${getCustomerEndpoint()}/universes/${universeUUID}/status`;
  const request = axios.get(requestUrl);
  return {
    type: GET_UNIVERSE_PER_NODE_STATUS,
    payload: request
  };
}

export function getUniversePerNodeStatusResponse(response) {
  return {
    type: GET_UNIVERSE_PER_NODE_STATUS_RESPONSE,
    payload: response
  };
}

export function getNodeDetails(universeUUID, nodeName) {
  const customerUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/nodes/${nodeName}/details`;
  const request = axios.get(requestUrl);
  return {
    type: GET_NODE_DETAILS,
    payload: request
  };
}

export function getNodeDetailsResponse(response) {
  return {
    type: GET_NODE_DETAILS_RESPONSE,
    payload: response
  };
}

export function resetNodeDetails() {
  return {
    type: RESET_NODE_DETAILS
  };
}

export function getUniversePerNodeMetrics(universeUUID) {
  const requestUrl = `${getCustomerEndpoint()}/universes/${universeUUID}/tablet-servers`;
  const request = axios.get(requestUrl);
  return {
    type: GET_UNIVERSE_PER_NODE_METRICS,
    payload: request
  };
}

export function getUniversePerNodeMetricsResponse(response) {
  return {
    type: GET_UNIVERSE_PER_NODE_METRICS_RESPONSE,
    payload: response
  };
}

export function performUniverseNodeAction(universeUUID, nodeName, actionType) {
  const requestUrl = `${getCustomerEndpoint()}/universes/${universeUUID}/nodes/${nodeName}`;
  const request = axios.put(requestUrl, { nodeAction: actionType });
  return {
    type: PERFORM_UNIVERSE_NODE_ACTION,
    payload: request
  };
}

export function performUniverseNodeActionResponse(response) {
  return {
    type: PERFORM_UNIVERSE_NODE_ACTION_RESPONSE,
    payload: response
  };
}

export function getMasterLeader(universeUUID) {
  const request = axios.get(`${getCustomerEndpoint()}/universes/${universeUUID}/leader`);
  return {
    type: GET_MASTER_LEADER,
    payload: request
  };
}

export function getMasterLeaderResponse(response) {
  return {
    type: GET_MASTER_LEADER_RESPONSE,
    payload: response
  };
}

export function getMasterInfos(universeUUID) {
  const request = axios.get(`${getCustomerEndpoint()}/universes/${universeUUID}/master_infos`);
  return {
    type: GET_MASTER_INFO,
    payload: request
  };
}

export function getMasterInfosResponse(response) {
  return {
    type: GET_MASTER_INFO_RESPONSE,
    payload: response
  };
}

export function resetMasterLeader() {
  return {
    type: RESET_MASTER_LEADER
  };
}

export function checkIfUniverseExists(universeName) {
  const customerUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${customerUUID}/universes/find?name=${universeName}`;
  const request = axios.get(requestUrl);
  return {
    type: CHECK_IF_UNIVERSE_EXISTS,
    payload: request
  };
}

export function resetRollingUpgrade() {
  return {
    type: RESET_ROLLING_UPGRADE
  };
}

export function setUniverseMetrics(values) {
  return {
    type: SET_UNIVERSE_METRICS,
    payload: values
  };
}

export function setPlacementStatus(currentStatus) {
  return {
    type: SET_PLACEMENT_STATUS,
    payload: currentStatus
  };
}

export function resetUniverseConfiguration() {
  return {
    type: RESET_UNIVERSE_CONFIGURATION
  };
}

export function fetchUniverseMetadata() {
  return {
    type: FETCH_UNIVERSE_METADATA
  };
}

export function fetchUniverseBackups(universeUUID) {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/backups`);
  return {
    type: FETCH_UNIVERSE_BACKUPS,
    payload: request
  };
}

export function fetchUniverseBackupsResponse(response) {
  return {
    type: FETCH_UNIVERSE_BACKUPS_RESPONSE,
    payload: response
  };
}

export function resetUniverseBackups() {
  return {
    type: RESET_UNIVERSE_BACKUPS
  };
}

export function createUniverseBackup(universeUUID, formValues) {
  const cUUID = localStorage.getItem('customerId');
  const request = axios.put(
    `${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/multi_table_backup`,
    formValues
  );
  return {
    type: CREATE_UNIVERSE_BACKUP,
    payload: request
  };
}

export function createUniverseBackupResponse(response) {
  return {
    type: CREATE_UNIVERSE_BACKUP_RESPONSE,
    payload: response
  };
}

export function getHealthCheck(universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const request = axios.get(
    `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/health_check`
  );
  return {
    type: GET_HEALTH_CHECK,
    payload: request
  };
}

export function getHealthCheckResponse(response) {
  return {
    type: GET_HEALTH_CHECK_RESPONSE,
    payload: response
  };
}

export function setEncryptionKey(universeUUID, data) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/set_key`;
  const request = axios.post(endpoint, data);
  return {
    type: SET_ENCRYPTION_KEY,
    payload: request
  };
}

export function setEncryptionKeyResponse(response) {
  return {
    type: SET_ENCRYPTION_KEY_RESPONSE,
    payload: response
  };
}

export function updateBackupState(universeUUID, flag) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/update_backup_state?markActive=${flag}`;
  const request = axios.put(endpoint);

  return {
    type: UPDATE_BACKUP_STATE,
    payload: request
  };
}

export function updateBackupStateResponse(response) {
  return {
    type: UPDATE_BACKUP_STATE_RESPONSE,
    payload: response
  };
}

export function fetchLiveQueries(universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/live_queries`;
  return axios.get(endpoint);
}

export function fetchSlowQueries(universeUUID, cancelFn) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/slow_queries`;
  let request;
  if (cancelFn) {
    const CancelToken = axios.CancelToken;
    request = axios.get(endpoint, {
      cancelToken: new CancelToken(cancelFn)
    });
  } else {
    request = axios.get(endpoint);
  }

  return request;
}

export function resetSlowQueries(universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/slow_queries`;
  return axios.delete(endpoint);
}

export function getAlertTemplates(filter) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/alert_templates`;
  return axios.post(endpoint, filter).then((resp) => resp.data);
}

export function getAlertConfigurations(filter) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/alert_configurations/list`;
  return axios.post(endpoint, filter).then((resp) => resp.data);
}

export function createAlertConfiguration(data) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/alert_configurations`;
  return axios.post(endpoint, data);
}

export function updateAlertConfiguration(data) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/alert_configurations/${data.uuid}`;
  return axios.put(endpoint, data);
}

export function downloadLogs(universeUUID, nodeName) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/${nodeName}/download_logs`;
  window.open(endpoint, '_blank');
}

//G-Flags
export async function fetchGFlags(dbVersion, params) {
  try {
    const request = await axios.get(`${ROOT_URL}/metadata/version/${dbVersion}/list_gflags`, {
      params
    });
    return request;
  } catch (e) {
    throw e.response.data;
  }
}

export async function fetchParticularFlag(dbVersion, params) {
  try {
    const request = await axios.get(`${ROOT_URL}/metadata/version/${dbVersion}/gflag`, {
      params
    });
    return request;
  } catch (e) {
    throw e.response.data;
  }
}

export async function validateGFlags(dbVersion, payload) {
  try {
    const apiToken = Cookies.get('apiToken') ?? localStorage.getItem('apiToken');
    if (apiToken && apiToken !== '') {
      axios.defaults.headers.common['X-AUTH-YW-API-TOKEN'] = apiToken;
    }
    axios.defaults.headers.common['Csrf-Token'] = Cookies.get('csrfCookie');
    const request = await axios.post(
      `${ROOT_URL}/metadata/version/${dbVersion}/validate_gflags`,
      payload
    );
    return request;
  } catch (e) {
    throw e.response.data;
  }
}

//Fetch releases by provider
export async function fetchSupportedReleases(pUUID) {
  const cUUID = localStorage.getItem('customerId');
  try {
    return await axios.get(`${ROOT_URL}/customers/${cUUID}/providers/${pUUID}/releases`);
  } catch (e) {
    throw e.response.data;
  }
}

export function validateHelmYAML(UniverseConfigureTaskParams) {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/validate_kubernetes_overrides`, {
    ...UniverseConfigureTaskParams
  });
}

export async function fetchNodeDetails(universeUUID, nodeName) {
  const customerUUID = localStorage.getItem('customerId');
  try {
    return await axios.get(
      `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/nodes/${nodeName}/details`
    );
  } catch (e) {
    throw e.response.data;
  }
}
