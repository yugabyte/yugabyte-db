// Copyright (c) YugaByte, Inc.

import axios from 'axios';

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

// Commissioner Tasks for Universe
export const FETCH_UNIVERSE_TASKS = 'FETCH_UNIVERSE_TASKS';
export const FETCH_UNIVERSE_TASKS_SUCCESS = 'FETCH_UNIVERSE_TASKS_SUCCESS';
export const FETCH_UNIVERSE_TASKS_FAILURE = 'FETCH_UNIVERSE_TASKS_FAILURE';
export const RESET_UNIVERSE_TASKS = 'RESET_UNIVERSE_TASKS';

const ROOT_URL = location.href.indexOf('localhost') > 0 ? 'http://localhost:9000/api' : '/api';


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

export function fetchUniverseTasks(universeUUID) {
  var customerUUID = localStorage.getItem("customer_id");
  const request =
    axios.get(`${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/tasks`);
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

export function resetUniverseTasks(error) {
  return {
    type: RESET_UNIVERSE_TASKS
  }
}
