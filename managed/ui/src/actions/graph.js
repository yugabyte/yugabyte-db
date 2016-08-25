// Copyright (c) YugaByte, Inc.

export const CHANGE_GRAPH_FILTER = 'CHANGE_GRAPH_FILTER';
export const RESET_GRAPH_FILTER = 'RESET_GRAPH_FILTER';

export function changeGraphFilter(filterParams) {
  return {
    type: CHANGE_GRAPH_FILTER,
    payload: filterParams
  }
}

export function resetGraphFilter() {
  return {
    type: RESET_GRAPH_FILTER
  };
}
