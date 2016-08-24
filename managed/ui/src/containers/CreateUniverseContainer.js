// Copyright (c) YugaByte Inc.

import CreateUniverse from '../components/CreateUniverse.js';
import {getRegionList, getRegionListSuccess, getRegionListFailure, getProviderList, getProviderListSuccess, getProviderListFailure,
  getInstanceTypeList, getInstanceTypeListSuccess, getInstanceTypeListFailure} from '../actions/cloud';
import {createUniverse, createUniverseSuccess, createUniverseFailure} from '../actions/universe';
import { connect } from 'react-redux';

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universeName: state.universe.name,
    universeProvider: state.universe.provider,
    universeRegion: state.universe.regions,
    universeInstanceType: state.universe.instanceType,
    universeProviderList: state.cloud.providers,
    universeRegionList: state.cloud.regions,
    universeInstanceTypeList: state.cloud.instanceTypes
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    getProviderListItems: () => {
      dispatch(getProviderList())
        .then((response) => {
          if(response.payload.status !== 200) {
            dispatch(getProviderListFailure(response.payload));
          } else {
            dispatch(getProviderListSuccess(response.payload));
          }
        });
    },

    getRegionListItems: (provider,isMultiAz) => {
      dispatch(getRegionList(provider, isMultiAz))
        .then((response) => {
          if(response.payload.status !== 200) {
            dispatch(getRegionListFailure(response.payload));
          } else {
            dispatch(getRegionListSuccess(response.payload));
          }
        });
    },

    getInstanceTypeListItems: (provider) => {
      dispatch(getInstanceTypeList(provider))
        .then((response) => {
          if(response.payload.status !== 200) {
            dispatch(getInstanceTypeListFailure(response.payload));
          } else {
            dispatch(getInstanceTypeListSuccess(response.payload));
          }
        });
    },
    
    createNewUniverse: (formData) => {
      dispatch(createUniverse(formData)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(createUniverseFailure(response.payload));
        } else {
          dispatch(createUniverseSuccess(response.payload));
        }
      });
    }

  }
}

export default connect(mapStateToProps,mapDispatchToProps)(CreateUniverse);
