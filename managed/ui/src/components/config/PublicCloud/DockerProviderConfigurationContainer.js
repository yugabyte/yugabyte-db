// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import { DockerProviderConfiguration } from '../../config';
import { createDockerProvider, createDockerProviderResponse,
 deleteProvider, deleteProviderFailure, deleteProviderSuccess, fetchCloudMetadata } from '../../../actions/cloud';
import { openDialog, closeDialog } from '../../../actions/universe';

const mapStateToProps = (state) => {
  return {
    configuredProviders: state.cloud.providers,
    configuredRegions: state.cloud.supportedRegionList,
    accessKeys: state.cloud.accessKeys,
    initialValues: { accountName: "Docker" },
    dockerBootstrap: state.cloud.dockerBootstrap,
    universeList: state.universe.universeList,
    universeLoading: state.universe.loading.universeList,
    visibleModal: state.universe.visibleModal,
    // TODO change this once we refactor aws bootstrap.
    cloudBootstrap: state.cloud.bootstrap
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    createProvider: (name) => {
      dispatch(createDockerProvider()).then((response) => {
        dispatch(createDockerProviderResponse(response.payload));
      });
    },

    deleteProviderConfig: (providerUUID) => {
      dispatch(deleteProvider(providerUUID)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(deleteProviderFailure(response.payload));
        } else {
          dispatch(deleteProviderSuccess(response.payload));
        }
      })
    },

    showDeleteProviderModal: () => {
      dispatch(openDialog("deleteDockerProvider"));
    },

    hideDeleteProviderModal: () => {
      dispatch(closeDialog());
    },

    reloadCloudMetadata: () => {
      dispatch(fetchCloudMetadata());
    }
  }
}

var dockerConfigForm = reduxForm({
  form: 'dockerConfigForm'
})

export default connect(mapStateToProps, mapDispatchToProps)(dockerConfigForm(DockerProviderConfiguration));
