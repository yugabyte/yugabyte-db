// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import {OnPremConfiguration} from '../../config';
import { createOnPremProvider, createOnPremProviderResponse, getProviderList,
  getProviderListResponse } from '../../../actions/cloud';

const mapStateToProps = (state) => {
  return {
    cloud: state.cloud, // the state populated by "reducer_cloud.js"
    config: state.config // the state populated by "reducer_config.js"
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    createOnPremProvider: (config) => {

      // Set up payload for createOnPremProvider
      let configJson = JSON.parse(config);
      configJson["identification"]["type"] = "onprem";
      configJson["identification"]["uuid"] = "";

      // Dispatch and handle createOnPremProvider
      dispatch(createOnPremProvider(configJson)).then((createResponse) => {
        let createPayload = createResponse.payload;
        dispatch(createOnPremProviderResponse(createPayload));

        // Dispatch and handle getProviderList
        if(createPayload.status === 200) {
          dispatch(getProviderList()).then((listProvidersResponse) => {
            dispatch(getProviderListResponse(listProvidersResponse.payload));
          });
        }
      });
    }
  }
};

export default connect(mapStateToProps, mapDispatchToProps)(OnPremConfiguration);
