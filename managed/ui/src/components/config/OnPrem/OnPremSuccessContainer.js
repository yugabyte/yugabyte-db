// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { isObject } from 'lodash';
import {reset} from 'redux-form';
import { OnPremSuccess } from '../../config';
import { deleteProvider, deleteProviderSuccess, deleteProviderFailure, fetchCloudMetadata, listAccessKeysResponse, listAccessKeys } from '../../../actions/cloud';
import { isNonEmptyArray } from 'utils/ObjectUtils';
import {openDialog, closeDialog} from '../../../actions/universe';

const mapStateToProps = (state) => {
  return {
    universeList: state.universe.universeList,
    configuredProviders: state.cloud.providers,
    configuredRegions: state.cloud.supportedRegionList,
    accessKeys: state.cloud.accessKeys,
    cloudBootstrap: state.cloud.bootstrap,
    visibleModal: state.universe.visibleModal
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    deleteProviderConfig: (providerUUID) => {
      dispatch(deleteProvider(providerUUID)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(deleteProviderFailure(response.payload));
        } else {
          dispatch(deleteProviderSuccess(response.payload));
        }
      });
    },

    fetchAccessKeysList: (providerUUID) => {
      dispatch(listAccessKeys(providerUUID)).then((response) => {
        dispatch(listAccessKeysResponse(response.payload));
      })
    },

    showDeleteProviderModal: () => {
      dispatch(openDialog("deleteOnPremProvider"));
    },

    hideDeleteProviderModal: () => {
      dispatch(closeDialog());
    },

    fetchCloudMetadata: () => {
      dispatch(fetchCloudMetadata());
    },

    resetConfigForm: () => {
      dispatch(reset("onPremConfigForm"));
    }
  }
};

export default connect(mapStateToProps, mapDispatchToProps)(OnPremSuccess);
