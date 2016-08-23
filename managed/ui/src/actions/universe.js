// Copyright (c) YugaByte, Inc.

import axios from 'axios';

export const FETCH_UNIVERSE_INFO = 'FETCH_UNIVERSE_INFO';
export const FETCH_UNIVERSE_INFO_SUCCESS = 'FETCH_UNIVERSE_INFO_SUCCESS';
export const FETCH_UNIVERSE_INFO_FAILURE = 'FETCH_UNIVERSE_INFO_FAILURE';
export const RESET_UNIVERSE_INFO = 'RESET_UNIVERSE_INFO';

const ROOT_URL = location.href.indexOf('localhost') > 0 ? 'http://localhost:9000/api' : '/api';

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
