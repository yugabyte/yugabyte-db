// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { toast } from 'react-toastify';
import CreateKubernetesConfiguration from './CreateKubernetesConfiguration';
import {
  createProviderResponse,
  createMultiRegionKubernetesProvider,
  fetchCloudMetadata,
  getKubeConfig
} from '../../../../actions/cloud';
import { openDialog, closeDialog } from '../../../../actions/modal';

const mapStateToProps = (state) => {
  return {
    modal: state.modal,
    customer: state.customer,
    providers: state.cloud.providers,
    featureFlags: state.featureFlags
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    createKubernetesProvider: (providerName, providerConfig, regionData) => {
      dispatch(createMultiRegionKubernetesProvider(providerName, providerConfig, regionData)).then(
        (response) => {
          dispatch(createProviderResponse(response.payload));
          if (response.payload.status === 200) {
            dispatch(fetchCloudMetadata());
          } else {
            const error = `Failed to create provider: ${
              response.payload?.response?.data?.error || response.payload.message
            }`;
            toast.error(error);
          }
        }
      );

    },
    fetchKubenetesConfig: () => {
        return getKubeConfig();
    },
    showModal: (modalName) => {
      dispatch(openDialog(modalName));
    },
    closeModal: () => {
      dispatch(closeDialog());
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(CreateKubernetesConfiguration);
