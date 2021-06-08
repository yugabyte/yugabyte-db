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
  editCustomerConfig,
  editCustomerConfigResponse,
  setInitialValues
} from '../../../actions/customers';
import { openDialog, closeDialog } from '../../../actions/modal';

const mapStateToProps = (state) => {
  return {
    addConfig: state.customer.addConfig,
    editConfig: state.customer.editConfig,
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

    setInitialValues: (initialValues) => {
      return dispatch(setInitialValues(initialValues));
    },

    editCustomerConfig: (config) => {
      return dispatch(editCustomerConfig(config)).then((response) => {
        return dispatch(editCustomerConfigResponse(response.payload));
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
