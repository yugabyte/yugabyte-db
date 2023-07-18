// Copyright (c) YugaByte, Inc.
import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import { StorageConfiguration } from '..';
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
import { handleCACertErrMsg } from '../../customCACerts';

// TODO: Add specific types or replace with hooks.
//       This file was converted to Typescript to resolve type errors
//       when using this component in Typescript files.
const customerConfigToasterHandler = (errorMessageObject: any) => {
  isNonEmptyObject(errorMessageObject)
    ? Object.keys(errorMessageObject).forEach((errorKey) => {
        toast.error(
          <ul>
            {errorMessageObject[errorKey].map((error: any) => (
              // eslint-disable-next-line react/jsx-key
              <li>{error}</li>
            ))}
          </ul>
        );
      })
    : toast.error(errorMessageObject);
};

const mapStateToProps = (state: any) => {
  return {
    addConfig: state.customer.addConfig,
    editConfig: state.customer.editConfig,
    customerConfigs: state.customer.configs,
    visibleModal: state.modal.visibleModal,
    deleteConfig: state.customer.deleteConfig,
    initialValues: state.customer.setInitialVal
  };
};

const mapDispatchToProps = (dispatch: any) => {
  return {
    addCustomerConfig: (config: any) => {
      return dispatch(addCustomerConfig(config)).then((response: any) => {
        if (response.error) {
          if(handleCACertErrMsg(response.payload)){
            return;
          }
          const errorMessageObject =
            response.payload?.response?.data?.error || response.payload.message;
          customerConfigToasterHandler(errorMessageObject);
        } else {
          toast.success('Successfully added the backup configuration.');
        }
        return dispatch(addCustomerConfigResponse(response.payload));
      });
    },

    setInitialValues: (initialValues: any) => {
      return dispatch(setInitialValues(initialValues));
    },

    editCustomerConfig: (config: any) => {
      return dispatch(editCustomerConfig(config)).then((response: any) => {
        if (response.error) {
          if(handleCACertErrMsg(response.payload)){
            return;
          }
          const errorMessageObject =
            response.payload?.response?.data?.error || response.payload.message;
          customerConfigToasterHandler(errorMessageObject);
        } else {
          toast.success('Successfully updated the backup configuration.');
        }
        return dispatch(editCustomerConfigResponse(response.payload));
      });
    },

    deleteCustomerConfig: (configUUID: string) => {
      return dispatch(deleteCustomerConfig(configUUID)).then((response: any) => {
        return dispatch(deleteCustomerConfigResponse(response.payload));
      });
    },

    fetchCustomerConfigs: () => {
      dispatch(fetchCustomerConfigs()).then((response: any) => {
        dispatch(fetchCustomerConfigsResponse(response.payload));
      });
    },

    showDeleteStorageConfig: (configName: any) => {
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

export default connect<{}, {}, any>(
  mapStateToProps,
  mapDispatchToProps
)(storageConfigForm(StorageConfiguration));
