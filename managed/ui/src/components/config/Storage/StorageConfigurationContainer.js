// Copyright (c) YugaByte, Inc.
import React from 'react';
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
import { toast } from 'react-toastify';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

const customerConfigToasterHandler = (errorMessageObject) => {
  isNonEmptyObject(errorMessageObject)
    ? Object.keys(errorMessageObject).forEach((errorKey) => {
        toast.error(
          <ul>
            {errorMessageObject[errorKey].map((error) => (
              <li>{error}</li>
            ))}
          </ul>
        );
      })
    : toast.error(errorMessageObject);
};

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
        if (response.error) {
          const errorMessageObject =
            response.payload?.response?.data?.error || response.payload.message;
            customerConfigToasterHandler(errorMessageObject);
          
        } else {
          toast.success('Successfully added the backup configuration.');
        }
        return dispatch(addCustomerConfigResponse(response.payload));
      });
    },

    setInitialValues: (initialValues) => {
      return dispatch(setInitialValues(initialValues));
    },

    editCustomerConfig: (config) => {
      return dispatch(editCustomerConfig(config)).then((response) => {
        if (response.error) {
          const errorMessageObject =
            response.payload?.response?.data?.error || response.payload.message;
            customerConfigToasterHandler(errorMessageObject);
          
        } else {
          toast.success('Successfully updated the backup configuration.');
        }
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
