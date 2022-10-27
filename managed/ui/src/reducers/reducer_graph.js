// Copyright (c) YugaByte, Inc.

import {
  CHANGE_GRAPH_QUERY_PERIOD,
  RESET_GRAPH_QUERY_PERIOD,
  QUERY_METRICS,
  QUERY_METRICS_SUCCESS,
  QUERY_METRICS_FAILURE,
  SELECTED_METRIC_TYPE_TAB,
  RESET_METRICS,
  TOGGLE_PROMETHEUS_QUERY
} from '../actions/graph';
import { DEFAULT_GRAPH_FILTER } from '../components/metrics/index';

const INITIAL_STATE = {
  graphFilter: DEFAULT_GRAPH_FILTER,
  metrics: {},
  loading: false,
  error: null,
  universeMetricList: [],
  prometheusQueryEnabled: true,
  tabName: null
};

export default function (state = INITIAL_STATE, action) {
  switch (action.type) {
    case CHANGE_GRAPH_QUERY_PERIOD:
      const filters = { ...action.payload };
      return { ...state, graphFilter: filters };
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
      return { ...state, metrics: metricData, loading: false };
    }
    case SELECTED_METRIC_TYPE_TAB: {
      return { ...state, tabName: action.tabName };
    }
    case QUERY_METRICS_FAILURE: {
      const metricData = state.metrics;
      metricData[action.panelType] = { error: true };
      return { ...state, metrics: metricData, error: action.payload.response, loading: false };
    }
    case RESET_METRICS:
      // Graph Filter needs to be reset when user jumps to metrics view in different places
      return {
        ...state,
        metrics: {},
        loading: false,
        panelType: null,
        graphFilter: DEFAULT_GRAPH_FILTER
      };
    case TOGGLE_PROMETHEUS_QUERY:
      return { ...state, prometheusQueryEnabled: !state.prometheusQueryEnabled };
    default:
      return state;
  }
}
