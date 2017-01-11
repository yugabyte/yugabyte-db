// Copyright (c) YugaByte, Inc.

// Set OnPremJsonConfigData
export const SET_ON_PREM_CONFIG_DATA = 'SET_ON_PREM_CONFIG_DATA';

export function setOnPremConfigData(configData) {
  return {
    type: SET_ON_PREM_CONFIG_DATA,
    payload: configData
  };
}
