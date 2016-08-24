// Copyright (c) YugaByte, Inc.

import axios from 'axios';

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

const ROOT_URL = location.href.indexOf('localhost') > 0 ? 'http://localhost:9000/api' : '/api';


export function validateToken(tokenFromStorage) {
  var cUUID = localStorage.getItem("customer_id");
  var auth_token = localStorage.getItem("customer_token").toString();
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
  return {
    type: LOGOUT
  };
}

