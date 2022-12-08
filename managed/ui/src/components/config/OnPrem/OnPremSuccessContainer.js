// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { destroy } from 'redux-form';
import { OnPremSuccess } from '..';
import {
  deleteProvider,
  deleteProviderSuccess,
  deleteProviderFailure,
  fetchCloudMetadata,
  setOnPremConfigData,
  getNodeInstancesForProvider,
  getNodesInstancesForProviderResponse,
  getInstanceTypeList,
  getInstanceTypeListResponse,
  getInstanceTypeListLoading
} from '../../../actions/cloud';
import { openDialog, closeDialog } from '../../../actions/modal';
import { fetchUniverseList, fetchUniverseListResponse } from '../../../actions/universe';

const mapStateToProps = (state, ownProps) => {
  return {
    universeList: state.universe.universeList,
    configuredProviders: state.cloud.providers,
    configuredRegions: state.cloud.supportedRegionList,
    accessKeys: state.cloud.accessKeys,
    cloudBootstrap: state.cloud.bootstrap,
    visibleModal: state.modal.visibleModal,
    cloud: state.cloud
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    deleteProviderConfig: (providerUUID) => {
      return dispatch(deleteProvider(providerUUID)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(deleteProviderFailure(response.payload));
        } else {
          dispatch(deleteProviderSuccess(response.payload));
        }
      });
    },
    fetchUniverseList: () => {
      return new Promise((resolve) => {
        dispatch(fetchUniverseList()).then((response) => {
          dispatch(fetchUniverseListResponse(response.payload));
          resolve(response.payload.data);
        });
      });
    },
    showDeleteProviderModal: () => {
      dispatch(openDialog('deleteOnPremProvider'));
    },

    hideDeleteProviderModal: () => {
      dispatch(closeDialog());
    },

    fetchCloudMetadata: () => {
      dispatch(fetchCloudMetadata());
    },

    resetConfigForm: () => {
      dispatch(destroy('onPremConfigForm'));
    },
    fetchConfiguredNodeList: (pUUID) => {
      dispatch(getInstanceTypeListLoading());
      return dispatch(getNodeInstancesForProvider(pUUID)).then((response) => {
        dispatch(getNodesInstancesForProviderResponse(response.payload));
      });
    },
    setOnPremJsonData: (jsonData) => {
      dispatch(setOnPremConfigData(jsonData));
    },
    fetchInstanceTypeList: (pUUID) => {
      dispatch(getInstanceTypeListLoading());
      return dispatch(getInstanceTypeList(pUUID)).then((response) => {
        dispatch(getInstanceTypeListResponse(response.payload));
      });
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(OnPremSuccess);
