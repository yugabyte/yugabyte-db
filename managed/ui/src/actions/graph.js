// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

export const CHANGE_GRAPH_QUERY_PERIOD = 'CHANGE_GRAPH_QUERY_PERIOD';
export const RESET_GRAPH_QUERY_PERIOD = 'RESET_GRAPH_QUERY_PERIOD';

export const QUERY_CUSTOMER_METRICS = 'QUERY_CUSTOMER_METRICS';
export const QUERY_CUSTOMER_METRICS_SUCCESS = 'QUERY_CUSTOMER_METRICS_SUCCESS';
export const QUERY_CUSTOMER_METRICS_FAILURE = 'QUERY_CUSTOMER_METRICS_FAILURE';

export const QUERY_UNIVERSE_METRICS = 'QUERY_UNIVERSE_METRICS';
export const QUERY_UNIVERSE_METRICS_SUCCESS = 'QUERY_UNIVERSE_METRICS_SUCCESS';
export const QUERY_UNIVERSE_METRICS_FAILURE = 'QUERY_UNIVERSE_METRICS_FAILURE';
export const RESET_METRICS = 'RESET_METRICS';

export function changeGraphQueryPeriod(params) {
  return {
    type: CHANGE_GRAPH_QUERY_PERIOD,
    payload: params
  }
}

export function resetGraphQueryPeriod() {
  return {
    type: RESET_GRAPH_QUERY_PERIOD
  };
}

export function queryCustomerMetrics(queryParams) {
  var customerUUID = localStorage.getItem("customer_id");
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/metrics`, queryParams);
  return {
    type: QUERY_CUSTOMER_METRICS,
    payload: request
  };
}

export function queryCustomerMetricsSuccess(result) {
  return {
    type: QUERY_CUSTOMER_METRICS_SUCCESS,
    payload: result
  };
}

export function queryCustomerMetricsFailure(error) {
  return {
    type: QUERY_CUSTOMER_METRICS_FAILURE,
    payload: error
  }
}

export function queryUniverseMetrics(universeUUID, queryParams) {
  var customerUUID = localStorage.getItem("customer_id");
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/metrics`, queryParams);
  return {
    type: QUERY_UNIVERSE_METRICS,
    payload: request
  };
}

export function queryUniverseMetricsSuccess(result) {
  return {
    type: QUERY_UNIVERSE_METRICS_SUCCESS,
    payload: result
  };
}

export function queryUniverseMetricsFailure(error) {
  return {
    type: QUERY_UNIVERSE_METRICS_FAILURE,
    payload: error
  }
}

export function resetMetrics() {
  return {
    type: RESET_METRICS
  };
}
