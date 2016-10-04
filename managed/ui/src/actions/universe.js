// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { isValidObject } from '../utils/ObjectUtils';
import { ROOT_URL } from '../config';

// Create Universe
export const CREATE_UNIVERSE = 'CREATE_NEW_UNIVERSE';
export const CREATE_UNIVERSE_SUCCESS = 'CREATE_UNIVERSE_SUCCESS';
export const CREATE_UNIVERSE_FAILURE = 'CREATE_UNIVERSE_FAILURE';

// Edit Universe
export const EDIT_UNIVERSE = 'EDIT_UNIVERSE';
export const EDIT_UNIVERSE_SUCCESS = 'EDIT_UNIVERSE_SUCCESS';
export const EDIT_UNIVERSE_FAILURE = 'EDIT_UNIVERSE_FAILURE';

// Get Universe
export const FETCH_UNIVERSE_INFO = 'FETCH_UNIVERSE_INFO';
export const FETCH_UNIVERSE_INFO_SUCCESS = 'FETCH_UNIVERSE_INFO_SUCCESS';
export const FETCH_UNIVERSE_INFO_FAILURE = 'FETCH_UNIVERSE_INFO_FAILURE';
export const RESET_UNIVERSE_INFO = 'RESET_UNIVERSE_INFO';

// Get List Of Universe
export const FETCH_UNIVERSE_LIST = 'FETCH_UNIVERSE_LIST';
export const FETCH_UNIVERSE_LIST_SUCCESS = 'FETCH_UNIVERSE_LIST_SUCCESS';
export const FETCH_UNIVERSE_LIST_FAILURE = 'FETCH_UNIVERSE_LIST_FAILURE';
export const RESET_UNIVERSE_LIST = 'RESET_UNIVERSE_LIST';

// Delete Universe
export const DELETE_UNIVERSE = 'DELETE_UNIVERSE';
export const DELETE_UNIVERSE_SUCCESS = 'DELETE_UNIVERSE_SUCCESS';
export const DELETE_UNIVERSE_FAILURE = 'DELETE_UNIVERSE_FAILURE';

// Get Universe Cost
export const FETCH_CUSTOMER_COST = 'FETCH_CUSTOMER_COST';
export const FETCH_CUSTOMER_COST_SUCCESS = 'FETCH_CUSTOMER_COST_SUCCESS';
export const FETCH_CUSTOMER_COST_FAILURE = 'FETCH_CUSTOMER_COST_FAILURE';
export const RESET_CUSTOMER_COST = 'RESET_CUSTOMER_COST';

// Commissioner Tasks for Universe
export const FETCH_UNIVERSE_TASKS = 'FETCH_UNIVERSE_TASKS';
export const FETCH_UNIVERSE_TASKS_SUCCESS = 'FETCH_UNIVERSE_TASKS_SUCCESS';
export const FETCH_UNIVERSE_TASKS_FAILURE = 'FETCH_UNIVERSE_TASKS_FAILURE';
export const RESET_UNIVERSE_TASKS = 'RESET_UNIVERSE_TASKS';

// Universe Modal Tasks
export const OPEN_DIALOG = 'OPEN_DIALOG';
export const CLOSE_DIALOG = 'CLOSE_DIALOG';

// Submit G-Flag Tasks
export const SUBMIT_GFLAGS = 'SUBMIT_GFLAGS';

export function createUniverse(formValues) {
  var customerUUID = localStorage.getItem("customer_id");
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/universes`, formValues);
  return {
    type: CREATE_UNIVERSE,
    payload: request
  };
}

export function createUniverseSuccess(result) {
  return {
    type: CREATE_UNIVERSE_SUCCESS,
    payload: result
  };
}

export function createUniverseFailure(error) {
  return {
    type: CREATE_UNIVERSE_FAILURE,
    payload: error
  }
}

export function fetchUniverseInfo(universeUUID) {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}`);
  return {
    type: FETCH_UNIVERSE_INFO,
    payload: request
  }
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

export function fetchUniverseList() {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/universes`);

  return {
    type: FETCH_UNIVERSE_LIST,
    payload: request
  }
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

export function deleteUniverse(universeUUID) {
  var customerUUID = localStorage.getItem("customer_id");
  const request=axios.delete(`${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}`);
  return {
    type: DELETE_UNIVERSE,
    payload: request
  };
}

export function deleteUniverseSuccess(result) {
  return {
    type: DELETE_UNIVERSE_SUCCESS,
    payload: result
  }
}

export function deleteUniverseFailure(error) {
  return {
    type: DELETE_UNIVERSE_FAILURE,
    payload: error
  }
}

export function editUniverse(universeUUID, formValues) {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.put(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}`, formValues);
  return {
    type: EDIT_UNIVERSE,
    payload: request
  }
}

export function editUniverseSuccess(result) {
  return {
    type: EDIT_UNIVERSE_SUCCESS,
    payload: result
  }
}

export function editUniverseFailure(error) {
  return {
    type: EDIT_UNIVERSE_FAILURE,
    payload: error
  }
}

export function fetchCustomerCost() {
  var customerUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/` + customerUUID + `/cost`);
  return {
    type: FETCH_CUSTOMER_COST,
    payload: request
  }
}

export function fetchCustomerCostSuccess(result) {
  return {
    type: FETCH_CUSTOMER_COST_SUCCESS,
    payload: result
  }
}

export function fetchCustomerCostFailue(error) {
  return {
    type: FETCH_CUSTOMER_COST_FAILURE,
    payload: error
  }
}

export function resetCustomerCost() {
  return {
    type: RESET_CUSTOMER_COST
  }
}

export function fetchUniverseTasks(universeUUID) {
  var customerUUID = localStorage.getItem("customer_id");

  var requestUrl;
  if (isValidObject(universeUUID)) {
    requestUrl = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/tasks`;
  } else {
    requestUrl = `${ROOT_URL}/customers/${customerUUID}/tasks`;
  }

  const request = axios.get(requestUrl);
  return {
    type: FETCH_UNIVERSE_TASKS,
    payload: request
  };
}

export function fetchUniverseTasksSuccess(result) {
  return {
    type: FETCH_UNIVERSE_TASKS_SUCCESS,
    payload: result
  };
}

export function fetchUniverseTasksFailure(error) {
  return {
    type: FETCH_UNIVERSE_TASKS_FAILURE,
    payload: error
  }
}

export function resetUniverseTasks() {
  return {
    type: RESET_UNIVERSE_TASKS
  }
}

export function openDialog(data) {
  return {
    type: OPEN_DIALOG,
    payload: data
  }
}

export function closeDialog() {
  return {
    type: CLOSE_DIALOG
  }
}

export function submitGFlags() {
  return {
    type: SUBMIT_GFLAGS
  }
}
