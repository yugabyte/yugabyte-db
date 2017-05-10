// Copyright (c) YugaByte, Inc.
import _ from 'lodash';
import { isDefinedNotNull } from '../utils/ObjectUtils';

export function setInitialState(object) {
  return {data: object, status: 'init', error: null};
}

export function setSuccessState(state, object, data) {
  return Object.assign({}, state, {
    [object]: { data: data, status: 'success', error: null }
  });
}

export function setLoadingState(state, object, data = null) {
  return Object.assign({}, state, {
    [object]: { data: data, status: 'loading'}
  });
}

export function setFailureState(state, object, error, data = null) {
  return Object.assign({}, state, {
    [object]: { data: data, status: 'failure', error: error }
  });
}

export function setResponseState(state, object, response) {
  const { payload: { data, status }} = response;
  let objectState = _.omit(response, ['payload', 'type']);
  if (status !== 200 || isDefinedNotNull(data.error)) {
    _.merge(objectState, { data: null, error: data.error, status: 'error' });
  } else {
    _.merge(objectState, { data: data, error: null, status: 'success' });
  }
  return Object.assign({}, state, { [object]: objectState });
}
