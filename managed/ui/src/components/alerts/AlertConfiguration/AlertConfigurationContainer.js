// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nissharma@deloitte.com)
//

import { connect } from 'react-redux';
import { toast } from 'react-toastify';
import { change } from 'redux-form';
import {
  alertConfigs,
  alertDestionations,
  createAlertDestination,
  createAlertDestinationResponse,
  createAlertReceiver,
  createAlertReceiverResponse,
  deleteAlertConfig,
  deleteAlertDestination,
  getAlertReceivers,
  setInitialValues,
  updateAlertDestination,
  updateAlertDestinationResponse,
  updateProfile,
  updateProfileFailure,
  updateProfileSuccess
} from '../../../actions/customers';
import { closeDialog, openDialog } from '../../../actions/modal';
import { fetchUniverseList, fetchUniverseListResponse } from '../../../actions/universe';
import { UniverseTaskList } from '../../universes/UniverseDetail/compounds/UniverseTaskList';
import { AlertConfiguration } from './AlertConfiguration';

const mapStateToProps = (state) => {
  return {
    customer: state.customer.currentCustomer,
    users: state.customer.users.data,
    apiToken: state.customer.apiToken,
    customerProfile: state.customer ? state.customer.profile : null,
    modal: state.modal,
    initialValues: state.customer.setInitialVal,
    universes: state.universe.universeList
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    alertConfigs: () => {
      return dispatch(alertConfigs()).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
          return;
        }
        return response.payload.data;
      });
    },
    alertDestionations: () => {
      return dispatch(alertDestionations()).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
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
          dispatch(updateProfileFailure(response.payload));
        } else {
          dispatch(updateProfileSuccess(response.payload));
        }
      });
    },
    createAlertChannel: (payload) => {
      return dispatch(createAlertReceiver(payload)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
        } else {
          toast.success('Successfully created the channel');
        }
        return dispatch(createAlertReceiverResponse(response.payload));
      });
    },
    getAlertReceivers: () => {
      return dispatch(getAlertReceivers()).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
          return;
        }
        return response.payload.data;
      });
    },
    createAlertDestination: (payload) => {
      return dispatch(createAlertDestination(payload)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
        } else {
          toast.success('Successfully added the destination');
        }
        return dispatch(createAlertDestinationResponse(response.payload));
      });
    },
    updateAlertDestination: (payload, uuid) => {
      return dispatch(updateAlertDestination(payload, uuid)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
        } else {
          toast.success('Successfully updated the destination');
        }
        return dispatch(updateAlertDestinationResponse(response.payload));
      });
    },
    deleteAlertDestination: (uuid) => {
      return dispatch(deleteAlertDestination(uuid)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
          return;
        }
        return response.payload.data;
      });
    },
    deleteAlertConfig: (uuid) => {
      return dispatch(deleteAlertConfig(uuid)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
          return;
        }
        return response.payload.data;
      });
    },
    fetchUniverseList: () => {
      dispatch(fetchUniverseList()).then((response) => {
        dispatch(fetchUniverseListResponse(response.payload));
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
