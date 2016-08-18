// Copyright (c) YugaByte, Inc.

import {GET_REGION_LIST,GET_REGION_LIST_SUCCESS,GET_REGION_LIST_FAILURE} from '../actions/cloud';

const INITIAL_STATE = {regions:null,providers:null,error:null};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case GET_REGION_LIST:
      return { ...state, regions: null, status:'storage', error:null, loading: true};
    case GET_REGION_LIST_SUCCESS:
      return { ...state, regions: action.payload.regions, status:'region_fetch_success', error:null, loading: false}; //<-- authenticated
    case GET_REGION_LIST_FAILURE:// return error and make loading = false
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, regions: null, status:'region_fetch_failure', error:error, loading: false};
    default:
      return state;
  }
}



