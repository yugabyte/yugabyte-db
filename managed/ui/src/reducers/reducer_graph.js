// Copyright (c) YugaByte, Inc.

import {
  CHANGE_GRAPH_QUERY_PERIOD,
  RESET_GRAPH_QUERY_PERIOD,
  QUERY_METRICS,
  QUERY_METRICS_SUCCESS,
  QUERY_METRICS_FAILURE,
  QUERY_MASTER_METRICS_SUCCESS,
  QUERY_MASTER_METRICS_FAILURE,
  SELECTED_METRIC_TYPE_TAB,
  RESET_METRICS,
  SET_GRAPH_FILTER,
  RESET_GRAPH_FILTER,
  TOGGLE_PROMETHEUS_QUERY
} from '../actions/graph';
import { DEFAULT_GRAPH_FILTER } from '../components/metrics/index';

const INITIAL_STATE = {
  graphFilter: DEFAULT_GRAPH_FILTER,
  metrics: {},
  masterMetrics: {},
  loading: false,
  error: null,
  universeMetricList: [],
  prometheusQueryEnabled: true,
  tabName: null
};

export default function (state = INITIAL_STATE, action) {
  switch (action.type) {
    case CHANGE_GRAPH_QUERY_PERIOD: {
      const filters = { ...action.payload };
      return { ...state, graphFilter: filters };
    }
    case RESET_GRAPH_QUERY_PERIOD:
      return { ...state, graphFilter: null };
    case QUERY_METRICS:
      return { ...state, loading: true };
    case QUERY_METRICS_SUCCESS: {
      const metricData = state.metrics;
      metricData[action.panelType] = {
        ...metricData[action.panelType],
        ...action.payload.data
      };
      const responseData = {
        ...state,
        loading: false,
        error: false
      };
      responseData.metrics = metricData;
      return responseData;
    }
    case QUERY_MASTER_METRICS_SUCCESS: {
      const metricData = state?.masterMetrics;
      metricData[action.panelType] = {
        ...metricData[action.panelType],
        ...action.payload.data
      };
      const responseData = {
        ...state,
        loading: false
      };
      responseData.masterMetrics = metricData;
      return responseData;
    }
    case SELECTED_METRIC_TYPE_TAB:
      return { ...state, tabName: action.tabName };
    case QUERY_METRICS_FAILURE: {
      const metricData = state.metrics;
      metricData[action.panelType] = { error: true };
      const responseData = {
        ...state,
        error: action.payload.response,
        loading: false
      };
      responseData.metrics = metricData;
      return responseData;
    }
    case SET_GRAPH_FILTER: {
      const filters = { ...action.payload };
      return { ...state, graphFilter: filters };
    }

    case QUERY_MASTER_METRICS_FAILURE: {
      const metricData = state.metrics;
      metricData[action.panelType] = { error: true };
      const responseData = {
        ...state,
        error: action.payload.response,
        loading: false
      };
      responseData.masterMetrics = metricData;
      return responseData;
    }
    case RESET_METRICS:
      return {
        ...state,
        metrics: {},
        masterMetrics: {},
        loading: false,
        panelType: null,
        error: null
      };
    case RESET_GRAPH_FILTER:
      // Graph Filter needs to be reset when user jumps to metrics view in different places
      return {
        ...state,
        graphFilter: DEFAULT_GRAPH_FILTER
      };
    case TOGGLE_PROMETHEUS_QUERY: {
      return { ...state, prometheusQueryEnabled: !state.prometheusQueryEnabled };
    }
    default:
      return state;
  }
}
