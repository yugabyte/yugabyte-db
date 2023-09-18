// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nissharma@deloitte.com)
//

import { connect } from 'react-redux';
import { toast } from 'react-toastify';
import { change } from 'redux-form';
import {
  alertConfigs,
  createAlertConfig,
  alertDestinations,
  createAlertConfigResponse,
  createAlertDestination,
  createAlertDestinationResponse,
  createAlertChannel,
  createAlertChannelResponse,
  deleteAlertConfig,
  deleteAlertDestination,
  getAlertChannels,
  getTargetMetrics,
  setInitialValues,
  updateAlertConfig,
  updateAlertConfigResponse,
  sendTestAlert,
  updateAlertDestination,
  updateAlertDestinationResponse,
  updateProfile,
  updateProfileFailure,
  updateProfileSuccess
} from '../../../actions/customers';
import { closeDialog, openDialog } from '../../../actions/modal';
import { fetchUniverseList, fetchUniverseListResponse } from '../../../actions/universe';
import { AlertConfiguration } from './AlertConfiguration';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { handleCACertErrMsg } from '../../customCACerts';

const mapStateToProps = (state) => {
  return {
    customer: state.customer.currentCustomer,
    users: state.customer.users.data,
    apiToken: state.customer.apiToken,
    customerProfile: state.customer ? state.customer.profile : null,
    modal: state.modal,
    initialValues: state.customer.setInitialVal,
    universes: state.universe.universeList,
    featureFlags: state.featureFlags
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    alertConfigs: (payload) => {
      return dispatch(alertConfigs(payload)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
          return;
        }
        return response.payload.data;
      });
    },
    alertDestinations: () => {
      return dispatch(alertDestinations()).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
          return;
        }
        return response.payload.data;
      });
    },
    getTargetMetrics: (payload) => {
      return dispatch(getTargetMetrics(payload)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
          return;
        }
        return response.payload.data;
      });
    },
    setInitialValues: (initialValues) => {
      return dispatch(setInitialValues(initialValues));
    },
    updateCustomerDetails: (values) => {
      dispatch(updateProfile(values)).then((response) => {
        if (response.payload.status !== 200) {
          toast.error('Configuration failed to update');
          dispatch(updateProfileFailure(response.payload));
        } else {
          toast.success('Configuration updated successfully');
          dispatch(updateProfileSuccess(response.payload));
        }
      });
    },
    createAlertChannel: (payload) => {
      return dispatch(createAlertChannel(payload)).then((response) => {
        if (response.error) {
          if (handleCACertErrMsg(response.payload)) {
            return;
          }
          toast.error(createErrorMessage(response.payload));
        } else {
          toast.success('Successfully created the channel');
        }
        return dispatch(createAlertChannelResponse(response.payload));
      });
    },
    getAlertChannels: () => {
      return dispatch(getAlertChannels()).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
          return;
        }
        return response.payload.data;
      });
    },
    createAlertDestination: (payload) => {
      return dispatch(createAlertDestination(payload)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
        } else {
          toast.success('Successfully added the destination');
        }
        return dispatch(createAlertDestinationResponse(response.payload));
      });
    },
    createAlertConfig: (payload) => {
      return dispatch(createAlertConfig(payload)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
        } else {
          toast.success('Successfully added the alert configuration');
        }
        return dispatch(createAlertConfigResponse(response.payload));
      });
    },
    updateAlertConfig: (payload, uuid) => {
      return dispatch(updateAlertConfig(payload, uuid)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
        } else {
          toast.success('Successfully updated the alert configuration');
        }
        return dispatch(updateAlertConfigResponse(response.payload));
      });
    },
    sendTestAlert: (uuid) => {
      sendTestAlert(uuid)
        .then((response) => {
          toast.success(response.data.message);
        })
        .catch((error) => {
          if (handleCACertErrMsg(error)) {
            return;
          }
          toast.error(createErrorMessage(error));
        });
    },
    updateAlertDestination: (payload, uuid) => {
      return dispatch(updateAlertDestination(payload, uuid)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
        } else {
          toast.success('Successfully updated the destination');
        }
        return dispatch(updateAlertDestinationResponse(response.payload));
      });
    },
    deleteAlertDestination: (uuid) => {
      return dispatch(deleteAlertDestination(uuid)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
          return;
        } else {
          toast.success('Successfully deleted the destination');
        }
        return response.payload.data;
      });
    },
    deleteAlertConfig: (uuid) => {
      return dispatch(deleteAlertConfig(uuid)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
          return;
        } else {
          toast.success('Successfully deleted the alert configuration');
        }
        return response.payload.data;
      });
    },
    fetchUniverseList: () => {
      return new Promise((resolve) => {
        dispatch(fetchUniverseList()).then((response) => {
          dispatch(fetchUniverseListResponse(response.payload));
          resolve(response.payload.data);
        });
      });
    },
    closeModal: () => {
      dispatch(closeDialog());
    },
    showAddChannelModal: () => {
      dispatch(openDialog('alertDestinationForm'));
    },
    showDeleteModal: (name) => {
      dispatch(openDialog(name));
    },
    showDetailsModal: () => {
      dispatch(openDialog('alertDestinationDetailsModal'));
    },
    updateField: (form, field, newValue) => dispatch(change(form, field, newValue))
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(AlertConfiguration);
