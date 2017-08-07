// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import GCPProviderConfiguration from './GCPProviderConfiguration';
import { reduxForm, reset } from 'redux-form';
import {createProvider, createProviderResponse,
        createRegion, createRegionResponse, createAccessKey, createAccessKeyResponse,
        initializeProvider, initializeProviderSuccess, initializeProviderFailure,
        fetchCloudMetadata, deleteProvider, deleteProviderResponse } from '../../../actions/cloud';
import { openDialog, closeDialog } from '../../../actions/universe';

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud,
    configuredProviders: state.cloud.providers,
    configuredRegions: state.cloud.supportedRegionList,
    cloudBootstrap: state.cloud.bootstrap,
    visibleModal: state.universe.visibleModal
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    createGCPProvider: (providerName, providerConfig) => {
      dispatch(createProvider("gcp", providerName, providerConfig)).then((response) => {
        dispatch(createProviderResponse(response.payload));
      });
    },
    createGCPRegions: (providerUUID, regionFormData) => {
      dispatch(createRegion(providerUUID, regionFormData)).then((response) => {
        dispatch(createRegionResponse(response.payload));
      });
    },
    createGCPAccessKey: (providerUUID, zone, key) => {
      dispatch(createAccessKey(providerUUID, zone, key)).then((response) => {
        dispatch(createAccessKeyResponse(response.payload));
      })
    },
    initializeGCPMetadata: (providerUUID) => {
      dispatch(initializeProvider(providerUUID)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(initializeProviderFailure(response.payload));
        } else {
          dispatch(initializeProviderSuccess(response.payload));
        }
      });
    },
    reloadCloudMetadata: () => {
      dispatch(fetchCloudMetadata());
    },
    showDeleteProviderModal: () => {
      dispatch(openDialog("deleteGCPProvider"));
    },
    hideDeleteProviderModal: () => {
      dispatch(closeDialog());
    },
    deleteProviderConfig: (providerUUID) => {
      dispatch(deleteProvider(providerUUID)).then((response) => {
        dispatch(deleteProviderResponse(response.payload));
        dispatch(reset('gcpConfigForm'));
      });
    },
  }
}

let gcpConfigForm = reduxForm({
  form: 'gcpConfigForm',
  fields: [ 'accountName'],
})

export default connect(mapStateToProps, mapDispatchToProps)(gcpConfigForm(GCPProviderConfiguration));
