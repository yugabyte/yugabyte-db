// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import ProviderConfiguration from './ProviderConfiguration';
import { reset } from 'redux-form';
import { createProvider, createProviderResponse, createRegion, createRegionResponse,
  createAccessKey, createAccessKeyResponse, initializeProvider, initializeProviderSuccess,
  initializeProviderFailure, deleteProvider, deleteProviderSuccess, deleteProviderFailure,
  resetProviderBootstrap, fetchCloudMetadata, bootstrapProvider, bootstrapProviderResponse } from '../../../actions/cloud';
import { openDialog, closeDialog } from '../../../actions/universe';
import {fetchTaskProgress, fetchTaskProgressResponse,fetchCustomerTasks , fetchCustomerTasksFailure, fetchCustomerTasksSuccess }
  from '../../../actions/tasks';

const mapDispatchToProps = (dispatch) => {
  return {
    createAWSProvider: (type, name, config, regionFormVals) => {
      dispatch(createProvider(type, name, config)).then((response) => {
        dispatch(createProviderResponse(response.payload));
        if (response.payload.status === 200) {
          dispatch(fetchCloudMetadata());
          const providerUUID = response.payload.data.uuid;
          dispatch(bootstrapProvider(providerUUID, regionFormVals)).then((boostrapResponse) => {
            dispatch(bootstrapProviderResponse(boostrapResponse.payload));
          });
        }
      });
    },

    createGCPProvider: (providerName, providerConfig) => {
      dispatch(createProvider("gcp", providerName, providerConfig)).then((response) => {
        dispatch(createProviderResponse(response.payload));
        if (response.payload.status === 200) {
          dispatch(fetchCloudMetadata());
          const providerUUID = response.payload.data.uuid;
          const params = {"regionList": ["us-west1"], "hostVpcId": ""};
          dispatch(bootstrapProvider(providerUUID, params)).then((boostrapResponse) => {
            dispatch(bootstrapProviderResponse(boostrapResponse.payload));
          });
        }
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
          dispatch(fetchCloudMetadata());
          dispatch(reset('awsConfigForm'));
        }
      });
    },

    resetProviderBootstrap: () => {
      dispatch(resetProviderBootstrap());
    },

    // Valid Provider Types are
    // deleteGCPProvider, deleteAWSProvider
    showDeleteProviderModal: (providerType) => {
      dispatch(openDialog(providerType));
    },

    hideDeleteProviderModal: () => {
      dispatch(closeDialog());
    },

    reloadCloudMetadata: () => {
      dispatch(fetchCloudMetadata());
    },

    getCurrentTaskData: (taskUUID) => {
      dispatch(fetchTaskProgress(taskUUID)).then((response) => {
        dispatch(fetchTaskProgressResponse(response.payload));
      });
    },

    fetchCustomerTasksList: () => {
      dispatch(fetchCustomerTasks()).then((response) => {
        if (response.payload.status === 200) {
          dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    }
  };
};


const mapStateToProps = (state) => {
  return {
    configuredProviders: state.cloud.providers,
    configuredRegions: state.cloud.supportedRegionList,
    accessKeys: state.cloud.accessKeys,
    cloudBootstrap: state.cloud.bootstrap,
    universeList: state.universe.universeList,
    hostInfo: state.customer.hostInfo,
    visibleModal: state.universe.visibleModal,
    cloud: state.cloud,
    tasks: state.tasks,
  };
};


export default connect(mapStateToProps, mapDispatchToProps)(ProviderConfiguration);
