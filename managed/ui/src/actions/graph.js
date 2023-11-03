// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

export const CHANGE_GRAPH_QUERY_PERIOD = 'CHANGE_GRAPH_QUERY_PERIOD';
export const RESET_GRAPH_QUERY_PERIOD = 'RESET_GRAPH_QUERY_PERIOD';

export const QUERY_METRICS = 'QUERY_METRICS';
export const QUERY_METRICS_SUCCESS = 'QUERY_METRICS_SUCCESS';
export const QUERY_MASTER_METRICS_SUCCESS = 'QUERY_MASTER_METRICS_SUCCESS';
export const QUERY_MASTER_METRICS_FAILURE = 'QUERY_MASTER_METRICS_FAILURE';
export const QUERY_METRICS_FAILURE = 'QUERY_METRICS_FAILURE';
export const SET_GRAPH_FILTER = 'SET_GRAPH_FILTER';
export const RESET_METRICS = 'RESET_METRICS';
export const RESET_GRAPH_FILTER = 'RESET_GRAPH_FILTER';
export const SELECTED_METRIC_TYPE_TAB = 'SELECTED_METRIC_TYPE_TAB';

export const TOGGLE_PROMETHEUS_QUERY = 'TOGGLE_PROMETHEUS_QUERY';

export function changeGraphQueryPeriod(params) {
  return {
    type: CHANGE_GRAPH_QUERY_PERIOD,
    payload: params
  };
}

export function resetGraphQueryPeriod() {
  return {
    type: RESET_GRAPH_QUERY_PERIOD
  };
}

export function queryMetrics(queryParams) {
  const customerUUID = localStorage.getItem('customerId');
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/metrics`, queryParams);
  return {
    type: QUERY_METRICS,
    payload: request
  };
}

export function getQueryMetrics(queryParams) {
  const customerUUID = localStorage.getItem('customerId');
  return axios
    .post(`${ROOT_URL}/customers/${customerUUID}/metrics`, queryParams)
    .then((resp) => resp.data);
}

export function queryMetricsSuccess(result, panelType) {
  return {
    type: QUERY_METRICS_SUCCESS,
    payload: result,
    panelType: panelType
  };
}

export function queryMasterMetricsSuccess(result, panelType) {
  return {
    type: QUERY_MASTER_METRICS_SUCCESS,
    payload: result,
    panelType: panelType
  };
}

export function queryMetricsFailure(error, panelType) {
  return {
    type: QUERY_METRICS_FAILURE,
    payload: error,
    panelType: panelType
  };
}

export function queryMasterMetricsFailure(error, panelType) {
  return {
    type: QUERY_MASTER_METRICS_FAILURE,
    payload: error,
    panelType: panelType
  };
}

export function currentTabSelected(tabName) {
  return {
    type: SELECTED_METRIC_TYPE_TAB,
    tabName: tabName
  };
}

export function resetMetrics() {
  return {
    type: RESET_METRICS
  };
}

export function setGraphFilter(filter) {
  return {
    type: SET_GRAPH_FILTER,
    payload: filter
  };
}

export function resetGraphFilter() {
  return {
    type: RESET_GRAPH_FILTER
  };
}

export function togglePrometheusQuery() {
  return {
    type: TOGGLE_PROMETHEUS_QUERY
  };
}

export function getGrafanaJson() {
  return axios.get(`${ROOT_URL}/grafana_dashboard`);
}
