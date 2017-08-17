// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';
export const FETCH_TABLES_LIST = 'FETCH_TABLES_LIST';
export const FETCH_TABLES_LIST_SUCCESS = 'FETCH_TABLES_LIST_SUCCESS';
export const FETCH_TABLES_LIST_FAILURE = 'FETCH_TABLES_LIST_FAILURE';
export const RESET_TABLES_LIST = 'RESET_TABLES_LIST';
export const FETCH_TABLE_DETAIL = 'FETCH_TABLE_DETAIL';
export const FETCH_TABLE_DETAIL_SUCCESS = 'FETCH_TABLE_DETAIL_SUCCESS';
export const FETCH_TABLE_DETAIL_FAILURE = 'FETCH_TABLE_DETAIL_FAILURE';
export const RESET_TABLE_DETAIL = 'RESET_TABLE_DETAIL';
export const CREATE_UNIVERSE_TABLE = 'CREATE_UNIVERSE_TABLE';
export const CREATE_UNIVERSE_TABLE_SUCCESS = 'CREATE_UNIVERSE_TABLE_SUCCESS';
export const CREATE_UNIVERSE_TABLE_FAILURE = 'CREATE_UNIVERSE_TABLE_FAILURE';
export const FETCH_COLUMN_TYPES = 'FETCH_COLUMN_TYPES';
export const FETCH_COLUMN_TYPES_SUCCESS = 'FETCH_COLUMN_TYPES_SUCCESS';
export const FETCH_COLUMN_TYPES_FAILURE = 'FETCH_COLUMN_TYPES_FAILURE';
export const TOGGLE_TABLE_VIEW = 'TOGGLE_TABLE_VIEW';
export const BULK_IMPORT = 'BULK_IMPORT';
export const BULK_IMPORT_RESPONSE = 'BULK_IMPORT_RESPONSE';

export function fetchUniverseTables(universeUUID) {
  const customerId = localStorage.getItem("customer_id");
  const request =
    axios.get(`${ROOT_URL}/customers/${customerId}/universes/${universeUUID}/tables`);
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
  };
}


export function fetchTableDetail(universeUUID, tableUUID) {
  const customerId = localStorage.getItem("customer_id");
  const request =
    axios.get(`${ROOT_URL}/customers/${customerId}/universes/${universeUUID}/tables/${tableUUID}`);
  return {
    type: FETCH_TABLE_DETAIL,
    payload: request
  };
}

export function resetTableDetail() {
  return {
    type: RESET_TABLE_DETAIL
  };
}

export function createUniverseTable(universeUUID, formValues) {
  const customerId = localStorage.getItem("customer_id");
  const request =
    axios.post(`${ROOT_URL}/customers/${customerId}/universes/${universeUUID}/tables`, formValues);
  return {
    type: CREATE_UNIVERSE_TABLE,
    payload: request
  };
}


export function fetchTableDetailSuccess(result) {
  return {
    type: FETCH_TABLE_DETAIL_SUCCESS,
    payload: result
  };
}

export function fetchTableDetailFailure(error) {
  return {
    type: FETCH_TABLE_DETAIL_FAILURE,
    payload: error
  };
}


export function createUniverseTableSuccess(result) {
  return {
    type: CREATE_UNIVERSE_TABLE_SUCCESS,
    payload: result
  };
}

export function createUniverseTableFailure(error) {
  return {
    type: CREATE_UNIVERSE_TABLE_FAILURE,
    payload: error
  };
}

export function fetchColumnTypes() {
  const request = axios.get(`${ROOT_URL}/metadata/column_types`);
  return {
    type: FETCH_COLUMN_TYPES,
    payload: request
  };
}

export function fetchColumnTypesSuccess(result) {
  return {
    type: FETCH_COLUMN_TYPES_SUCCESS,
    payload: result
  };
}

export function fetchColumnTypesFailure(error) {
  return {
    type: FETCH_COLUMN_TYPES_FAILURE,
    payload: error
  };
}

export function toggleTableView(currentView) {
  return {
    type: TOGGLE_TABLE_VIEW,
    payload: currentView
  };
}

export function resetTablesList() {
  return {
    type: RESET_TABLES_LIST
  };
}

export function bulkImport(universeUUID, tableUUID, formValues) {
  const customerId = localStorage.getItem("customer_id");
  const request = axios.put(
    `${ROOT_URL}/customers/${customerId}/universes/${universeUUID}/tables/${tableUUID}/bulk_import`,
    formValues
  );
  return {
    type: BULK_IMPORT,
    payload: request
  };
}

export function bulkImportResponse(response) {
  return {
    type: BULK_IMPORT_RESPONSE,
    payload: response
  };
}
