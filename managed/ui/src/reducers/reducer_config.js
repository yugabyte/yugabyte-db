// Copyright (c) YugaByte, Inc.

import { SET_ON_PREM_CONFIG_DATA } from '../actions/config';

const INITIAL_STATE = {onPremJsonFormData: []};

export default function(state = INITIAL_STATE, action) {
  switch(action.type) {
    case SET_ON_PREM_CONFIG_DATA:
      return {...state, onPremJsonFormData: action.payload};
    default:
      return state;
  }
}

