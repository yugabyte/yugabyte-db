// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { AWSProviderConfiguration } from '../../config';
import { reduxForm } from 'redux-form';
import { createProvider, createProviderSuccess, createProviderFailure,
  createRegion, createRegionSuccess, createRegionFailure,
  createAccessKey, createAccessKeySuccess, createAccessKeyFailure,
  initializeProvider, initializeProviderSuccess, initializeProviderFailure,
  getSupportedRegionData, getSupportedRegionDataSuccess, getSupportedRegionDataFailure,
  getRegionList, getRegionListSuccess, getRegionListFailure, getProviderList,
  getProviderListSuccess, getProviderListFailure,
 } from '../../../actions/cloud';

function validate(values) {
  var errors = {};
  var hasErrors = false;
  if (!values.accessKey || values.accessKey.trim() === '') {
    errors.accessKey = 'Access Key is required';
    hasErrors = true;
  }

  if(!values.secretKey || values.secretKey.trim() === '') {
    errors.secretKey = 'Secret Key is required';
    hasErrors = true;
  }
  return hasErrors && errors;
}

const mapDispatchToProps = (dispatch) => {
  return {
    createProvider: (type, config) => {
      dispatch(createProvider(type, config)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(createProviderFailure(response.payload));
        } else {
          dispatch(createProviderSuccess(response.payload));
        }
      });
    },
    createRegion: (providerUUID, regionCode) => {
      dispatch(createRegion(providerUUID, regionCode)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(createRegionFailure(response.payload));
        } else {
          dispatch(createRegionSuccess(response.payload));
        }

      });
    },
    createAccessKey: (providerUUID, regionUUID, accessKeyCode) => {
      dispatch(createAccessKey(providerUUID, regionUUID, accessKeyCode)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(createAccessKeyFailure(response.payload));
        } else {
          dispatch(createAccessKeySuccess(response.payload));
        }
      });
    },

    initializeProvider: (providerUUID) => {
      dispatch(initializeProvider(providerUUID)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(initializeProviderFailure(response.payload));
        } else {
          dispatch(initializeProviderSuccess(response.payload));
        }
      });
    },

    getSupportedRegionList: () => {
      dispatch(getSupportedRegionData()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(getSupportedRegionDataFailure(response.payload));
        } else {
          dispatch(getSupportedRegionDataSuccess(response.payload));
        }
      })
    },

    getProviderListItems: () => {
      dispatch(getProviderList()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(getProviderListFailure(response.payload));
        } else {
          dispatch(getProviderListSuccess(response.payload));
          response.payload.data.forEach(function (item, idx) {
            dispatch(getRegionList(item.uuid, true))
              .then((response) => {
                if (response.payload.status !== 200) {
                  dispatch(getRegionListFailure(response.payload));
                } else {
                  dispatch(getRegionListSuccess(response.payload));
                }
              });
          })}
      });
    }
  }

}


const mapStateToProps = (state) => {
  return {
    configuredRegions: state.cloud.supportedRegionList,
    cloudBootstrap: state.cloud.bootstrap
  };
}

var awsConfigForm = reduxForm({
  form: 'awsConfigForm',
  fields: ['accessKey', 'secretKey'],
  validate
})

export default connect(mapStateToProps, mapDispatchToProps)(awsConfigForm(AWSProviderConfiguration));
