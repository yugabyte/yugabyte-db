// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';
export const FETCH_TABLES_LIST = 'FETCH_TABLES_LIST';
export const FETCH_TABLES_LIST_SUCCESS = 'FETCH_TABLES_LIST_SUCCESS';
export const FETCH_TABLES_LIST_FAILURE = 'FETCH_TABLES_LIST_FAILURE';
export const CREATE_UNIVERSE_TABLE = 'CREATE_UNIVERSE_TABLE';
export const CREATE_UNIVERSE_TABLE_SUCCESS = 'CREATE_UNIVERSE_TABLE_SUCCESS';
export const CREATE_UNIVERSE_TABLE_FAILURE = 'CREATE_UNIVERSE_TABLE_FAILURE';

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

export function createUniverseTable(universeUUID, formValues) {
  var customerId = localStorage.getItem("customer_id");
  const request =
    axios.post(`${ROOT_URL}/${customerId}/universes/${universeUUID}/tables`, formValues);
  return {
    type: CREATE_UNIVERSE_TABLE,
    payload: request
  };
}

export function createUniverseTableSuccess(result) {
  return {
    type: CREATE_UNIVERSE_TABLE_SUCCESS,
    payload: result
  }
}

export function createUniverseTableFailure(error) {
  return {
    type: CREATE_UNIVERSE_TABLE_FAILURE,
    payload: error
  }
}

