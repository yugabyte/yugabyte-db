// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { AWSProviderConfiguration } from '../../config';
import { reduxForm, reset } from 'redux-form';
import { createProvider, createProviderSuccess, createProviderFailure,
  createRegion, createRegionSuccess, createRegionFailure,
  createAccessKey, createAccessKeySuccess, createAccessKeyFailure,
  initializeProvider, initializeProviderSuccess, initializeProviderFailure,
  getSupportedRegionData, getSupportedRegionDataSuccess, getSupportedRegionDataFailure,
  getRegionList, getRegionListSuccess, getRegionListFailure, getProviderList,
  getProviderListSuccess, getProviderListFailure, deleteProvider, deleteProviderSuccess,
  deleteProviderFailure, resetProviderBootstrap, listAccessKeys,
  listAccessKeysSuccess, listAccessKeysFailure
 } from '../../../actions/cloud';
 import { fetchHostInfo, fetchHostInfoSuccess,
   fetchHostInfoFailure } from '../../../actions/customers';

function validate(values) {
  var errors = {};
  var hasErrors = false;
  if (!values.accountName) {
    errors.accountName = 'Account Name is required';
    hasErrors = true;
  }

  if (/\s/.test(values.accountName)) {
    errors.accountName = 'Account Name cannot have spaces';
    hasErrors = true;
  }

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
    createProvider: (type, name, config) => {
      dispatch(createProvider(type, name, config)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(createProviderFailure(response.payload));
        } else {
          dispatch(createProviderSuccess(response.payload));
        }
      });
    },
    createRegion: (providerUUID, regionCode, hostVPCId) => {
      dispatch(createRegion(providerUUID, regionCode, hostVPCId)).then((response) => {
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

    deleteProviderConfig: (providerUUID) => {
      dispatch(deleteProvider(providerUUID)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(deleteProviderFailure(response.payload));
        } else {
          dispatch(deleteProviderSuccess(response.payload));
          dispatch(reset('awsConfigForm'));
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
            dispatch(listAccessKeys(item.uuid))
              .then((response) => {
                if (response.payload.status !== 200) {
                  dispatch(listAccessKeysFailure(response.payload));
                } else {
                  dispatch(listAccessKeysSuccess(response.payload));
                }
              });
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
    },

    resetProviderBootstrap: () => {
      dispatch(resetProviderBootstrap());
    },

    fetchHostInfo: () => {
      dispatch(fetchHostInfo()).then((response)=>{
        if (response.payload.status !== 200) {
          dispatch(fetchHostInfoFailure(response.payload));
        } else {
          dispatch(fetchHostInfoSuccess(response.payload));
        }
      })
    }
  }
}


const mapStateToProps = (state) => {
  return {
    configuredProviders: state.cloud.providers,
    configuredRegions: state.cloud.supportedRegionList,
    accessKeys: state.cloud.accessKeys,
    cloudBootstrap: state.cloud.bootstrap,
    initialValues: { accountName: "Amazon" },
    universeList: state.universe.universeList,
    universeLoading: state.universe.loading.universeList,
    hostInfo: state.customer.hostInfo

  };
}

var awsConfigForm = reduxForm({
  form: 'awsConfigForm',
  fields: ['accessKey', 'secretKey', 'accountName'],
  validate
})

export default connect(mapStateToProps, mapDispatchToProps)(awsConfigForm(AWSProviderConfiguration));
