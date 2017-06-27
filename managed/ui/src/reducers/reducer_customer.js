// Copyright (c) YugaByte, Inc.

import { VALIDATE_FROM_TOKEN, VALIDATE_FROM_TOKEN_RESPONSE,
         REGISTER, REGISTER_RESPONSE, LOGIN, LOGIN_RESPONSE, RESET_CUSTOMER,
         LOGOUT, LOGOUT_SUCCESS, LOGOUT_FAILURE, FETCH_SOFTWARE_VERSIONS_FAILURE, FETCH_SOFTWARE_VERSIONS_SUCCESS,
         FETCH_SOFTWARE_VERSIONS, FETCH_HOST_INFO, FETCH_HOST_INFO_SUCCESS, FETCH_HOST_INFO_FAILURE,
         FETCH_CUSTOMER_COUNT, UPDATE_PROFILE, UPDATE_PROFILE_SUCCESS, UPDATE_PROFILE_FAILURE } from '../actions/customers';
import {sortVersionStrings} from '../utils/ObjectUtils';
import { getInitialState, setLoadingState, setSuccessState, setFailureState, setPromiseResponse }  from '../utils/PromiseUtils';

const INITIAL_STATE = {
  currentCustomer: getInitialState({}),
  authToken: getInitialState({}),
  tasks: [],
  status: null,
  error: null,
  loading: false,
  softwareVersions: [],
  hostInfo: null,
  customerCount: {},
  profile: getInitialState({})
};

export default function(state = INITIAL_STATE, action) {
  switch(action.type) {
    case VALIDATE_FROM_TOKEN:
      return setLoadingState(state, "currentCustomer", {});
    case VALIDATE_FROM_TOKEN_RESPONSE:
      return setPromiseResponse(state, "currentCustomer", action);

    case REGISTER:
      return setLoadingState(state, "authToken", {})
    case REGISTER_RESPONSE:
      return setPromiseResponse(state, "authToken", action);

    case LOGIN:
      return setLoadingState(state, "authToken", {});
    case LOGIN_RESPONSE:
      return setPromiseResponse(state, "authToken", action);

    case LOGOUT:
      return {...state};
    case LOGOUT_SUCCESS:
      return {...state, currentCustomer: getInitialState({}), authToken: getInitialState({})};
    case LOGOUT_FAILURE:
      return {...state};
    case RESET_CUSTOMER:
      return {...state, currentCustomer: getInitialState({}), authToken: getInitialState({})};
    case FETCH_SOFTWARE_VERSIONS:
      return {...state, softwareVersions: []}
    case FETCH_SOFTWARE_VERSIONS_SUCCESS:
      return {...state, softwareVersions: sortVersionStrings(action.payload.data)}
    case FETCH_SOFTWARE_VERSIONS_FAILURE:
      return {...state}
    case FETCH_HOST_INFO:
      return {...state, hostInfo: null}
    case FETCH_HOST_INFO_SUCCESS:
      return {...state, hostInfo: action.payload.data}
    case FETCH_HOST_INFO_FAILURE:
      return {...state, hostInfo: null }

    case UPDATE_PROFILE:
      return setLoadingState(state, "profile")
    case UPDATE_PROFILE_SUCCESS:
      return setSuccessState(state, "profile", "updated-success")
    case UPDATE_PROFILE_FAILURE:
      return setFailureState(state, "profile", action.payload.data.error)
    case FETCH_CUSTOMER_COUNT:
      return setLoadingState(state, "customerCount");
    default:
      return state;
  }
}
