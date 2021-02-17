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
import { toast } from 'react-toastify';

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
      .catch(() => toast.error('Error occured while fetching config.'));
    },

    setKMSConfig: (provider, body) => {
      return dispatch(createKMSProviderConfig(provider, body))
        .then((response) => {
          return dispatch(createKMSProviderConfigResponse(response.payload)).then(
            () => toast.success('Successfully added the configuration')
          );
        })
        .catch((err) => toast.error(`Error submitting KMS configuration: ${err}`));
    },

    deleteKMSConfig: (configUUID) => {
      dispatch(deleteKMSProviderConfig(configUUID))
        .then((response) => {
          if (response.payload.status === 200) {
            toast.success('Successfully deleted KMS configuration');
            return dispatch(deleteKMSProviderConfigResponse(configUUID));
          }
          toast.warn('Warning: Deleting configuration returned unsuccessful response.');
        })
        .catch((err) => {
          console.error(err)
        });
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(KeyManagementConfiguration);
