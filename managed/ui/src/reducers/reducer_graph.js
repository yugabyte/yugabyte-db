// Copyright (c) YugaByte, Inc.

import {
  CHANGE_GRAPH_QUERY_PERIOD,
  RESET_GRAPH_QUERY_PERIOD,
  QUERY_METRICS,
  QUERY_METRICS_SUCCESS,
  QUERY_METRICS_FAILURE,
  RESET_METRICS,
  TOGGLE_PROMETHEUS_QUERY
} from '../actions/graph';
import { DEFAULT_GRAPH_FILTER } from '../components/metrics';

const INITIAL_STATE = {
  graphFilter: DEFAULT_GRAPH_FILTER,
  metrics: {},
  loading: false,
  error: null,
  universeMetricList: [],
  prometheusQueryEnabled: false,
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
    case QUERY_METRICS_FAILURE: {
      const metricData = state.metrics;
      metricData[action.panelType] = {};
      return { ...state, metrics: metricData, error: action.payload.response, loading: false };
    }
    case RESET_METRICS:
      return { ...state, metrics: {}, loading: false, panelType: null };
    case TOGGLE_PROMETHEUS_QUERY:
      return { ...state, prometheusQueryEnabled: !state.prometheusQueryEnabled };
    default:
      return state;
  }
}
