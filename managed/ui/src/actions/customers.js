// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

// Get current user(me) from token in localStorage
export const VALIDATE_FROM_TOKEN = 'VALIDATE_FROM_TOKEN';
export const VALIDATE_FROM_TOKEN_SUCCESS = 'VALIDATE_FROM_TOKEN_SUCCESS';
export const VALIDATE_FROM_TOKEN_FAILURE = 'VALIDATE_FROM_TOKEN_FAILURE';

// Sign Up Customer
export const REGISTER = 'REGISTER';
export const REGISTER_SUCCESS = 'REGISTER_SUCCESS';
export const REGISTER_FAILURE = 'REGISTER_FAILURE';

// Sign In Customer
export const LOGIN = 'LOGIN';
export const LOGIN_SUCCESS = 'LOGIN_SUCCESS';
export const LOGIN_FAILURE = 'LOGIN_FAILURE';

// log out Customer
export const LOGOUT = 'LOGOUT';
export const LOGOUT_SUCCESS = 'LOGOUT_SUCCESS';
export const LOGOUT_FAILURE = 'LOGOUT_FAILURE';

// Update Customer Profile
export const UPDATE_PROFILE = 'UPDATE_PROFILE';
export const UPDATE_PROFILE_SUCCESS = 'UPDATE_PROFILE_SUCCESS';
export const UPDATE_PROFILE_FAILURE = 'UPDATE_PROFILE_FAILURE';

// Fetch Software Versions for Customer
export const FETCH_SOFTWARE_VERSIONS = 'FETCH_SOFTWARE_VERSIONS';
export const FETCH_SOFTWARE_VERSIONS_SUCCESS = 'FETCH_SOFTWARE_VERSIONS_SUCCESS';
export const FETCH_SOFTWARE_VERSIONS_FAILURE = 'FETCH_SOFTWARE_VERSIONS_FAILURE';

export const FETCH_HOST_INFO = 'FETCH_HOST_INFO';
export const FETCH_HOST_INFO_SUCCESS = 'FETCH_HOST_INFO_SUCCESS';
export const FETCH_HOST_INFO_FAILURE = 'FETCH_HOST_INFO_FAILURE';

export function validateToken(tokenFromStorage) {
  var cUUID = localStorage.getItem("customer_id");
  var auth_token = localStorage.getItem("customer_token");
  axios.defaults.headers.common['X-AUTH-TOKEN'] = auth_token;
  const request = axios(`${ROOT_URL}/customers/${cUUID}`);
  return {
    type: VALIDATE_FROM_TOKEN,
    payload: request
  };
}

export function validateTokenSuccess(currentUser) {
  return {
    type: VALIDATE_FROM_TOKEN_SUCCESS,
    payload: currentUser.data
  };
}

export function validateTokenFailure(error) {
  return {
    type: VALIDATE_FROM_TOKEN_FAILURE,
    payload: error
  };
}

export function register(formValues) {
  const request = axios.post(`${ROOT_URL}/register`, formValues);
  return {
    type: REGISTER,
    payload: request
  };
}

export function registerSuccess(customer) {
  return {
    type: REGISTER_SUCCESS,
    payload: customer
  };
}

export function registerFailure(error) {
  return {
    type: REGISTER_FAILURE,
    payload: error
  };
}


export function login(formValues) {
  const request = axios.post(`${ROOT_URL}/login`, formValues);

  return {
    type: LOGIN,
    payload: request
  };
}

export function loginSuccess(customer) {
  return {
    type: LOGIN_SUCCESS,
    payload: customer
  };
}

export function loginFailure(error) {
  return {
    type: LOGIN_FAILURE,
    payload: error
  };
}

export function logout() {
  const request = axios.get(`${ROOT_URL}/logout`);
  return {
    type: LOGOUT,
    payload: request
  };
}

export function logoutSuccess() {
  return {
    type: LOGOUT_SUCCESS
  };
}

export function logoutFailure(error) {
  return {
    type: LOGOUT_FAILURE,
    payload: error
  }
}

export function updateProfile(values) {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.put(`${ROOT_URL}/customers/${cUUID}`, values);
  return {
    type: UPDATE_PROFILE,
    payload: request
  }
}

export function updateProfileSuccess(response) {
  return {
    type: UPDATE_PROFILE_SUCCESS,
    payload: response
  }
}

export function updateProfileFailure(error) {
  return {
    type: UPDATE_PROFILE_FAILURE,
    payload: error
  }
}

export function fetchSoftwareVersions() {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/releases`);
  return {
    type: FETCH_SOFTWARE_VERSIONS,
    payload: request
  }
}

export function fetchSoftwareVersionsSuccess(result) {
  return {
    type: FETCH_SOFTWARE_VERSIONS_SUCCESS,
    payload: result
  }
}

export function fetchSoftwareVersionsFailure(error) {
  return {
    type: FETCH_SOFTWARE_VERSIONS_FAILURE,
    payload: error
  }
}

export function fetchHostInfo() {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/host_info`);
  return {
    type: FETCH_HOST_INFO,
    payload: request
  }
}

export function fetchHostInfoSuccess(result) {
  return {
    type: FETCH_HOST_INFO_SUCCESS,
    payload: result
  }
}

export function fetchHostInfoFailure(error) {
  return {
    type: FETCH_HOST_INFO_FAILURE,
    payload: error
  }
}
