// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { KeyManagementConfiguration } from '../../config';
import {
  fetchCustomerConfigs,
  fetchCustomerConfigsResponse,
  fetchCustomerRunTimeConfigs,
  fetchCustomerRunTimeConfigsResponse
} from '../../../actions/customers';
import {
  createKMSProviderConfig,
  createKMSProviderConfigResponse,
  editKMSProviderConfig,
  editKMSProviderConfigResponse,
  fetchAuthConfigList,
  fetchAuthConfigListResponse,
  deleteKMSProviderConfig,
  deleteKMSProviderConfigResponse
} from '../../../actions/cloud';
import { fetchTaskProgress, fetchTaskProgressResponse } from '../../../actions/tasks';
import { toast } from 'react-toastify';
import { handleCACertErrMsg } from '../../customCACerts';
import {
  fetchHostInfo,
  fetchHostInfoSuccess,
  fetchHostInfoFailure
} from '../../../actions/customers';

const mapStateToProps = (state) => {
  return {
    customerConfigs: state.customer.configs,
    runtimeConfigs: state.customer.runtimeConfigs,
    configList: state.cloud.authConfig,
    visibleModal: state.modal.visibleModal,
    deleteConfig: state.customer.deleteConfig,
    modal: state.modal,
    featureFlags: state.featureFlags,
    currentUserInfo: state.customer.currentUser.data,
    hostInfo: state.customer.hostInfo
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    fetchCustomerConfigs: () => {
      return dispatch(fetchCustomerConfigs()).then((response) =>
        dispatch(fetchCustomerConfigsResponse(response.payload))
      );
    },
    fetchCustomerRuntimeConfigs: () => {
      return dispatch(fetchCustomerRunTimeConfigs(true)).then((response) =>
        dispatch(fetchCustomerRunTimeConfigsResponse(response.payload))
      );
    },
    fetchKMSConfigList: () => {
      return dispatch(fetchAuthConfigList())
        .then((response) => dispatch(fetchAuthConfigListResponse(response.payload)))
        .catch(() => toast.error('Error occurred while fetching config.'));
    },

    setKMSConfig: (provider, body) => {
      return dispatch(createKMSProviderConfig(provider, body))
        .then?.((response) => {
          if (response.error) {
            if (handleCACertErrMsg(response.payload)) {
              return;
            }
            const errorMessage =
              response.payload?.response?.data?.error || response.payload.message;
            toast.error(errorMessage, { autoClose: 2500 });
          } else {
            toast.warn('Please wait. KMS configuration is being added', { autoClose: 2500 });
            return dispatch(createKMSProviderConfigResponse(response.payload));
          }
        })
        .catch((err) => toast.error(`Error submitting KMS configuration: ${err}`));
    },

    updateKMSConfig: (configUUID, body) => {
      return dispatch(editKMSProviderConfig(configUUID, body))
        .then?.((response) => {
          if (response.error) {
            const errorMessage =
              response.payload?.response?.data?.error || response.payload.message;
            toast.error(errorMessage, { autoClose: 2500 });
          } else {
            toast.warn('Please wait. KMS configuration is being updated', { autoClose: 2500 });
            return dispatch(editKMSProviderConfigResponse(response.payload));
          }
        })
        .catch((err) => toast.error(`Error updating KMS configuration: ${err}`));
    },

    getCurrentTaskData: (taskUUID) => {
      return dispatch(fetchTaskProgress(taskUUID)).then((response) =>
        dispatch(fetchTaskProgressResponse(response.payload))
      );
    },

    deleteKMSConfig: (configUUID) => {
      return dispatch(deleteKMSProviderConfig(configUUID))
        .then((response) => {
          if (response.payload.status === 200) {
            toast.success('Successfully deleted KMS configuration', { autoClose: 2500 });
            return dispatch(deleteKMSProviderConfigResponse(configUUID));
          }
          toast.warn('Warning: Deleting configuration returned unsuccessful response.');
        })
        .catch((err) => {
          console.error(err);
        });
    },

    fetchHostInfo: () => {
      dispatch(fetchHostInfo()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(fetchHostInfoFailure(response.payload));
        } else {
          dispatch(fetchHostInfoSuccess(response.payload));
        }
      });
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(KeyManagementConfiguration);
