// Copyright (c) YugaByte Inc.

import UniverseModal from '../components/UniverseModal.js';
import { connect } from 'react-redux';
import { getRegionList, getRegionListSuccess, getRegionListFailure, getProviderList,
         getProviderListSuccess, getProviderListFailure,
         getInstanceTypeList, getInstanceTypeListSuccess, getInstanceTypeListFailure }
         from '../actions/cloud';
import { createUniverse, createUniverseSuccess, createUniverseFailure } from '../actions/universe';
import { editUniverse, editUniverseSuccess, editUniverseFailure } from '../actions/universe';

const mapStateToProps = (state) => {
  return {
    universe: state.universe,
    cloud: state.cloud
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

    getRegionListItems: (provider, isMultiAz) => {
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
    },

    editUniverse: (universeUUID, formData) => {
      dispatch(editUniverse(universeUUID, formData)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(editUniverseFailure(response.payload));
        } else {
          dispatch(editUniverseSuccess(response.payload));
        }
      });
    }
  }
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseModal);


