// Copyright (c) YugaByte, Inc.

import { CHANGE_GRAPH_FILTER, RESET_GRAPH_FILTER } from '../actions/graph';

const INITIAL_STATE = {graphFilter: null};

export default function(state = INITIAL_STATE, action) {
  switch(action.type) {
    case CHANGE_GRAPH_FILTER:
      return { ...state, graphFilter: action.payload};
    case RESET_GRAPH_FILTER:
      return { ...state, graphFilter: null};
    default:
      return state;
  }
}
