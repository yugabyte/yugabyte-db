// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import { StorageConfiguration } from '../../config';
import {
  addCustomerConfig,
  addCustomerConfigResponse,
  fetchCustomerConfigs,
  fetchCustomerConfigsResponse,
  deleteCustomerConfig,
  deleteCustomerConfigResponse,
  setInitialConfigValues,
  updateCustomerConfig,
  updateCustomerConfigResponse
} from '../../../actions/customers';
import { openDialog, closeDialog } from '../../../actions/modal';

const mapStateToProps = (state) => {
  return {
    addConfig: state.customer.addConfig,
    updateConfig: state.customer.updateConfig,
    customerConfigs: state.customer.configs,
    visibleModal: state.modal.visibleModal,
    deleteConfig: state.customer.deleteConfig,
    initialValues: state.customer.setInitialVal
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    addCustomerConfig: (config) => {
      return dispatch(addCustomerConfig(config)).then((response) => {
        return dispatch(addCustomerConfigResponse(response.payload));
      });
    },

    setInitialConfigValues: (initialValues) => {
      return dispatch(setInitialConfigValues(initialValues));
    },

    updateCustomerConfig: (config) => {
      return dispatch(updateCustomerConfig(config)).then((res) => {
        dispatch(updateCustomerConfigResponse(res.payload));
      });
    },

    deleteCustomerConfig: (configUUID) => {
      return dispatch(deleteCustomerConfig(configUUID)).then((response) => {
        return dispatch(deleteCustomerConfigResponse(response.payload));
      });
    },

    fetchCustomerConfigs: () => {
      dispatch(fetchCustomerConfigs()).then((response) => {
        dispatch(fetchCustomerConfigsResponse(response.payload));
      });
    },

    showDeleteStorageConfig: (configName) => {
      dispatch(openDialog('delete' + configName + 'StorageConfig'));
    },

    hideDeleteStorageConfig: () => {
      dispatch(closeDialog());
    }
  };
};

const storageConfigForm = reduxForm({
  form: 'storageConfigForm',
  enableReinitialize: true
});

export default connect(
  mapStateToProps,
  mapDispatchToProps
)(storageConfigForm(StorageConfiguration));
