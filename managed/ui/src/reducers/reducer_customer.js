// Copyright (c) YugaByte, Inc.

import {
  VALIDATE_FROM_TOKEN, VALIDATE_FROM_TOKEN_SUCCESS, VALIDATE_FROM_TOKEN_FAILURE,
	REGISTER, REGISTER_SUCCESS, REGISTER_FAILURE,
	LOGIN, LOGIN_SUCCESS,  LOGIN_FAILURE, LOGOUT
} from '../actions/customers';

const INITIAL_STATE = {customer: null, universes: [], status: null, error: null, loading: false};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case VALIDATE_FROM_TOKEN:
      return { ...state, customer: null, status: 'storage', error: null, loading: true};
    case VALIDATE_FROM_TOKEN_SUCCESS:
      return { ...state, customer: action.payload.name, universes: action.payload.universes, status: 'authenticated', error: null, loading: false}; //<-- authenticated
    case VALIDATE_FROM_TOKEN_FAILURE:// return error and make loading = false
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, customer: null, status: 'storage', error: error, loading: false};
    case REGISTER:// sign up user, set loading = true and status = register
      return { ...state, customer: null, status: 'register', error: null, loading: true};
    case REGISTER_SUCCESS://return user, status = authenticated and make loading = false
      return { ...state, customer: action.payload.data.authToken, status: 'authenticated', error: null, loading: false}; //<-- authenticated
    case REGISTER_FAILURE:// return error and make loading = false
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors      
      return { ...state, customer: null, status: 'register', error: error, loading: false};
    case LOGIN:// sign in user,  set loading = true and status = login
      return { ...state, customer: null, status: 'login', error: null, loading: true};
    case LOGIN_SUCCESS://return authenticated user,  make loading = false and status = authenticated
      return { ...state, customer: action.payload.data.authToken, status: 'authenticated', error:null, loading: false}; //<-- authenticated
    case LOGIN_FAILURE:// return error and make loading = false
    error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors      
      return { ...state, customer: null, status: 'login', error: error, loading: false};
    case LOGOUT:
      return {...state, customer: null, status: 'logout', error: null, loading: false};
    default:
      return state;
  }
}
