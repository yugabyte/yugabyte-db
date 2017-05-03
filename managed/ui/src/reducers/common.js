// Copyright (c) YugaByte, Inc.
import _ from 'lodash';
import { isDefinedNotNull } from '../utils/ObjectUtils';

export function setSuccessState(state, object, data) {
  return Object.assign({}, state, {
    [object]: { data: data, loading: false, error: null }
  });
}

export function setLoadingState(state, object, data = null) {
  return Object.assign({}, state, {
    [object]: { data: data, error: null, loading: true }
  });
}

export function setFailureState(state, object, error, data = null) {
  return Object.assign({}, state, {
    [object]: { data: data, loading: false, error: error }
  });
}

export function setResponseState(state, object, response) {
  const { payload: { data, status }} = response;
  let objectState = _.omit(response, ['payload', 'type']);
  if (status !== 200 || isDefinedNotNull(data.error)) {
    _.merge(objectState, { data: null, error: data.error, loading: false });
  } else {
    _.merge(objectState, { data: data, error: null, loading: false });
  }
  return Object.assign({}, state, { [object]: objectState });
}
