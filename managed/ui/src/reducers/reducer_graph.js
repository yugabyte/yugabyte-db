// Copyright (c) YugaByte, Inc.

import { CHANGE_GRAPH_QUERY_PERIOD, RESET_GRAPH_QUERY_PERIOD,
QUERY_METRICS, QUERY_METRICS_SUCCESS, QUERY_METRICS_FAILURE,
RESET_METRICS } from '../actions/graph';
import { DEFAULT_GRAPH_FILTER } from '../components/metrics'

const INITIAL_STATE = {graphFilter: DEFAULT_GRAPH_FILTER, metrics: {},
  loading: false, error: null};

export default function(state = INITIAL_STATE, action) {
  switch(action.type) {
    case CHANGE_GRAPH_QUERY_PERIOD:
      return { ...state, graphFilter: action.payload};
    case RESET_GRAPH_QUERY_PERIOD:
      return { ...state, graphFilter: null};
    case QUERY_METRICS:
      return { ...state, loading: true};
    case QUERY_METRICS_SUCCESS:
      var metricData = state.metrics;
      metricData[action.panelType] = action.payload.data
      return { ...state, metrics: metricData, loading: false};
    case QUERY_METRICS_FAILURE:
      metricData = state.metrics;
      metricData[action.panelType] = []
      return { ...state, metrics: metricData, error: action.payload,
        loading: false};
    case RESET_METRICS:
      return { ...state, metrics: [], loading: false, panelType: null};
    default:
      return state;
  }
}
