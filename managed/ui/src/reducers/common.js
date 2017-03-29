// Copyright (c) YugaByte, Inc.

import _ from 'lodash';

export function setSuccessState(currentState, object, data) {
  _.merge(currentState[object], { data: data, loading: false, error: null });
}

export function setLoadingState(currentState, object) {
  _.merge(currentState[object], { data: null, loading: true, error: null });
}

export function setFailureState(currentState, object, error) {
  _.merge(currentState[object], { data: null, loading: false, error: error });
}
