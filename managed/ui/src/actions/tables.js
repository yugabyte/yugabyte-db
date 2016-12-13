// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

export const FETCH_TABLES_LIST = 'FETCH_TABLES_LIST';
export const FETCH_TABLES_LIST_SUCCESS = 'FETCH_TABLES_LIST_SUCCESS';
export const FETCH_TABLES_LIST_FAILURE = 'FETCH_TABLES_LIST_FAILURE';
export const RESET_TABLES_LIST = 'RESET_TABLES_LIST';

export function fetchUniverseTables(universeUUID) {
  var customerId = localStorage.getItem("customer_id");
  const request =
    axios.get(`${ROOT_URL}/${customerId}/universes/${universeUUID}/tables`);
  return {
    type: FETCH_TABLES_LIST,
    payload: request
  };
}

export function fetchUniverseTablesSuccess(result) {
  return {
    type: FETCH_TABLES_LIST_SUCCESS,
    payload: result
  };
}

export function fetchUniverseTablesFailure(error) {
  return {
    type: FETCH_TABLES_LIST_FAILURE,
    payload: error
  }
}

export function resetUniverseTablesList() {
  return {
    type: RESET_TABLES_LIST
  }
}
