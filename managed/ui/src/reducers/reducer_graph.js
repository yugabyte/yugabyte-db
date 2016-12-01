// Copyright (c) YugaByte, Inc.

import { CHANGE_GRAPH_QUERY_PERIOD, RESET_GRAPH_QUERY_PERIOD,
QUERY_CUSTOMER_METRICS, QUERY_CUSTOMER_METRICS_SUCCESS, QUERY_CUSTOMER_METRICS_FAILURE,
RESET_METRICS, QUERY_UNIVERSE_METRICS, QUERY_UNIVERSE_METRICS_SUCCESS,
QUERY_UNIVERSE_METRICS_FAILURE } from '../actions/graph';
import { DEFAULT_GRAPH_FILTER } from '../components/metrics'

const INITIAL_STATE = {graphFilter: DEFAULT_GRAPH_FILTER, metrics: [], loading: false, error: null};

export default function(state = INITIAL_STATE, action) {
  switch(action.type) {
    case CHANGE_GRAPH_QUERY_PERIOD:
      return { ...state, graphFilter: action.payload};
    case RESET_GRAPH_QUERY_PERIOD:
      return { ...state, graphFilter: null};
    case QUERY_CUSTOMER_METRICS:
      return { ...state, metrics: [], loading: true};
    case QUERY_CUSTOMER_METRICS_SUCCESS:
      return { ...state, metrics: action.payload.data, loading: false};
    case QUERY_CUSTOMER_METRICS_FAILURE:
      return { ...state, metrics: [], error: action.payload, loading: false};
    case QUERY_UNIVERSE_METRICS:
      return { ...state, metrics: [], loading: true};
    case QUERY_UNIVERSE_METRICS_SUCCESS:
      return { ...state, metrics: action.payload.data, loading: false};
    case QUERY_UNIVERSE_METRICS_FAILURE:
      return { ...state, metrics: [], error: action.payload, loading: false};
    case RESET_METRICS:
      return { ...state, metrics: [], loading: false};
    default:
      return state;
  }
}
