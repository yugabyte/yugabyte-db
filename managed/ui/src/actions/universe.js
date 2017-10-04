// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';
import { getCustomerEndpoint } from "./common";

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

// Get the Master Leader for a Universe
export const GET_MASTER_LEADER = 'GET_MASTER_LEADER';
export const GET_MASTER_LEADER_RESPONSE = 'GET_MASTER_LEADER_RESPONSE';

// Commissioner Tasks for Universe
export const FETCH_UNIVERSE_TASKS = 'FETCH_UNIVERSE_TASKS';
export const FETCH_UNIVERSE_TASKS_RESPONSE = 'FETCH_UNIVERSE_TASKS_RESPONSE';
export const RESET_UNIVERSE_TASKS = 'RESET_UNIVERSE_TASKS';

// Universe Modal Tasks
export const OPEN_DIALOG = 'OPEN_DIALOG';
export const CLOSE_DIALOG = 'CLOSE_DIALOG';

// Submit G-Flag Tasks
export const ROLLING_UPGRADE = 'ROLLING_UPGRADE';
export const ROLLING_UPGRADE_RESPONSE = 'ROLLING_UPGRADE_RESPONSE';
export const RESET_ROLLING_UPGRADE = 'RESET_ROLLING_UPGRADE';

// Universe Template Tasks
export const CONFIGURE_UNIVERSE_TEMPLATE = 'CONFIGURE_UNIVERSE_TEMPLATE';
export const CONFIGURE_UNIVERSE_TEMPLATE_RESPONSE = 'CONFIGURE_UNIVERSE_TEMPLATE_RESPONSE';
export const CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS = 'CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS';

export const CONFIGURE_UNIVERSE_RESOURCES = 'CONFIGURE_UNIVERSE_RESOURCES';
export const CONFIGURE_UNIVERSE_RESOURCES_RESPONSE = 'CONFIGURE_UNIVERSE_RESOURCES_RESPONSE';

// Universe per-node status
export const GET_UNIVERSE_PER_NODE_STATUS = 'GET_UNIVERSE_PER_NODE_STATUS';
export const GET_UNIVERSE_PER_NODE_STATUS_RESPONSE = 'GET_UNIVERSE_PER_NODE_STATUS_RESPONSE';

//Validation Tasks
export const CHECK_IF_UNIVERSE_EXISTS = 'CHECK_IF_UNIVERSE_EXISTS';

// Set Universe Read Write Metrics
export const SET_UNIVERSE_METRICS = 'SET_UNIVERSE_METRICS';

// Set Placement Status
export const SET_PLACEMENT_STATUS = 'SET_PLACEMENT_STATUS';

// Reset Universe Configuration
export const RESET_UNIVERSE_CONFIGURATION = 'RESET_UNIVERSE_CONFIGURATION';

export const FETCH_UNIVERSE_METADATA = 'FETCH_UNIVERSE_METADATA';

export function createUniverse(formValues) {
  const customerUUID = localStorage.getItem("customer_id");
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
  const cUUID = localStorage.getItem("customer_id");
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

export function fetchUniverseList() {
  const cUUID = localStorage.getItem("customer_id");
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

export function deleteUniverse(universeUUID, isForceDelete) {
  const customerUUID = localStorage.getItem("customer_id");
  const deleteRequestPayload = {isForceDelete: isForceDelete};
  const request = axios.delete(`${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}`, {params: deleteRequestPayload});
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


export function editUniverse(formValues, universeUUID) {
  const cUUID = localStorage.getItem("customer_id");
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

export function fetchUniverseTasks(universeUUID) {
  const customerUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/tasks`);
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

export function openDialog(data) {
  return {
    type: OPEN_DIALOG,
    payload: data
  };
}

export function closeDialog() {
  return {
    type: CLOSE_DIALOG
  };
}

export function rollingUpgrade(values, universeUUID) {
  const customerUUID = localStorage.getItem("customer_id");
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/upgrade`, values);
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

export function configureUniverseTemplate(values) {
  const customerUUID = localStorage.getItem("customer_id");
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
  const customerUUID = localStorage.getItem("customer_id");
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

export function checkIfUniverseExists(universeName) {
  const customerUUID = localStorage.getItem("customer_id");
  const requestUrl = `${ROOT_URL}/customers/${customerUUID}/universes/find/${universeName}`;
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
