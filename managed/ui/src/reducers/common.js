// Copyright (c) YugaByte, Inc.

export function setSuccessState(currentState, object, data) {
  currentState[object] = { data: data, loading: false, error: null };
  return currentState;
}

export function setLoadingState(currentState, object, data = null) {
  currentState[object] = { data: data, loading: true, error: null };
  return currentState;
}

export function setFailureState(currentState, object, error, data = null) {
  currentState[object]={ data: data, loading: false, error: error };
  return currentState;
}
