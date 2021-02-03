// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { KeyManagementConfiguration } from '../../config';
import { fetchCustomerConfigs, fetchCustomerConfigsResponse } from '../../../actions/customers';
import {
  createKMSProviderConfig,
  createKMSProviderConfigResponse,
  fetchAuthConfigList,
  fetchAuthConfigListResponse,
  deleteKMSProviderConfig,
  deleteKMSProviderConfigResponse
} from '../../../actions/cloud';
import { addToast } from '../../../actions/toaster';

const mapStateToProps = (state) => {
  return {
    customerConfigs: state.customer.configs,
    configList: state.cloud.authConfig,
    visibleModal: state.modal.visibleModal,
    deleteConfig: state.customer.deleteConfig
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    fetchCustomerConfigs: () => {
      return dispatch(fetchCustomerConfigs()).then((response) =>
        dispatch(fetchCustomerConfigsResponse(response.payload))
      );
    },

    fetchKMSConfigList: () => {
      return dispatch(fetchAuthConfigList()).then((response) => 
        dispatch(fetchAuthConfigListResponse(response.payload))
      )
      .catch((err) => {
        dispatch(addToast({
          toast: {
            type: 'error',
            description: 'Error occured while fetching config.',
            position: "bottom-right",
            icon: "fa fa-warning fa-3x"
          }
        }))
      })
    },

    setKMSConfig: (provider, body) => {
      return dispatch(createKMSProviderConfig(provider, body))
        .then((response) => {
          dispatch(addToast({
            toast: {
              type: 'success',
              description: 'Success added configuration!!',
              position: "bottom-right",
              icon: "fa fa-check-circle fa-3x"
            }
          }))
          return dispatch(createKMSProviderConfigResponse(response.payload));
        })
        .catch((err) => console.err('Error submitting KMS configuration: ', err));
    },

    deleteKMSConfig: (configUUID) => {
      dispatch(deleteKMSProviderConfig('configUUID'))
        .then((response) => {
          if (response.payload.status === 200) {
            return dispatch(deleteKMSProviderConfigResponse(configUUID));
          }
          dispatch(addToast({
            toast: {
              type: 'error',
              description: 'Warning: Deleting configuration returned unsuccessful response.',
              position: "bottom-right",
              icon: "fa fa-warning fa-3x"
            }
          }))
          console.warn('Warning: Deleting configuration returned unsuccessful response.');
        })
        .catch((err) => {
          console.error(err)
        });
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(KeyManagementConfiguration);
