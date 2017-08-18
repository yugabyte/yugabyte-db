// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { AWSProviderConfiguration } from '../../config';
import { reduxForm, reset } from 'redux-form';
import { createProvider, createProviderResponse, createRegion, createRegionResponse,
  createAccessKey, createAccessKeyResponse, initializeProvider, initializeProviderSuccess,
  initializeProviderFailure, deleteProvider, deleteProviderSuccess, deleteProviderFailure,
  resetProviderBootstrap, fetchCloudMetadata } from '../../../actions/cloud';
import { openDialog, closeDialog } from '../../../actions/universe';

function validate(values) {
  const errors = {};
  let hasErrors = false;
  if (!values.accountName) {
    errors.accountName = 'Name is required';
    hasErrors = true;
  }

  if (/\s/.test(values.accountName)) {
    errors.accountName = 'Name cannot have spaces';
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
        dispatch(createProviderResponse(response.payload));
      });
    },

    createRegion: (providerUUID, formVals) => {
      dispatch(createRegion(providerUUID, formVals)).then((response) => {
        dispatch(createRegionResponse(response.payload));
      });
    },

    createAccessKey: (providerUUID, regionUUID, accessKeyCode) => {
      const keyInfo = {'code': accessKeyCode};
      dispatch(createAccessKey(providerUUID, regionUUID, keyInfo)).then((response) => {
        dispatch(createAccessKeyResponse(response.payload));
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

    deleteProviderConfig: (providerUUID) => {
      dispatch(deleteProvider(providerUUID)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(deleteProviderFailure(response.payload));
        } else {
          dispatch(deleteProviderSuccess(response.payload));
          dispatch(reset('awsConfigForm'));
        }
      });
    },

    resetProviderBootstrap: () => {
      dispatch(resetProviderBootstrap());
    },

    showDeleteProviderModal: () => {
      dispatch(openDialog("deleteAWSProvider"));
    },

    hideDeleteProviderModal: () => {
      dispatch(closeDialog());
    },

    reloadCloudMetadata: () => {
      dispatch(fetchCloudMetadata());
    }
  };
};


const mapStateToProps = (state) => {
  return {
    configuredProviders: state.cloud.providers,
    configuredRegions: state.cloud.supportedRegionList,
    accessKeys: state.cloud.accessKeys,
    cloudBootstrap: state.cloud.bootstrap,
    initialValues: { accountName: "Amazon" },
    universeList: state.universe.universeList,
    hostInfo: state.customer.hostInfo,
    visibleModal: state.universe.visibleModal
  };
};

const awsConfigForm = reduxForm({
  form: 'awsConfigForm',
  fields: ['accessKey', 'secretKey', 'accountName'],
  validate
});

export default connect(mapStateToProps, mapDispatchToProps)(awsConfigForm(AWSProviderConfiguration));
