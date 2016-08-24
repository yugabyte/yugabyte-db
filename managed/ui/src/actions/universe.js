// Copyright (c) YugaByte, Inc.

import axios from 'axios';

// Create Universe
export const CREATE_UNIVERSE = 'CREATE_NEW_UNIVERSE';
export const CREATE_UNIVERSE_SUCCESS = 'CREATE_UNIVERSE_SUCCESS';
export const CREATE_UNIVERSE_FAILURE = 'CREATE_UNIVERSE_FAILURE';

// Get Universe
export const FETCH_UNIVERSE_INFO = 'FETCH_UNIVERSE_INFO';
export const FETCH_UNIVERSE_INFO_SUCCESS = 'FETCH_UNIVERSE_INFO_SUCCESS';
export const FETCH_UNIVERSE_INFO_FAILURE = 'FETCH_UNIVERSE_INFO_FAILURE';
export const RESET_UNIVERSE_INFO = 'RESET_UNIVERSE_INFO';

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

export function createUniverseFailure(error){
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
