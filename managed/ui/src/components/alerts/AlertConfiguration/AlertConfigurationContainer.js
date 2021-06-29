// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nissharma@deloitte.com)
//
// TODO: Redux still needs to be configured once the API contract
// will be finalized and be available.

import { connect } from 'react-redux';
import {
  alertConfigs,
  alertDestionations,
  updateProfile,
  updateProfileFailure,
  updateProfileSuccess
} from '../../../actions/customers';
import { closeDialog, openDialog } from '../../../actions/modal';
import { AlertConfiguration } from './AlertConfiguration';

const mapStateToProps = (state) => {
  return {
    customer: state.customer.currentCustomer,
    users: state.customer.users.data,
    apiToken: state.customer.apiToken,
    customerProfile: state.customer ? state.customer.profile : null,
    modal: state.modal
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    alertConfigs: () => {
      return dispatch(alertConfigs());
    },
    alertDestionations: () => {
      return dispatch(alertDestionations());
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

    closeModal: () => {
      dispatch(closeDialog());
    },
    showAddChannelModal: () => {
      dispatch(openDialog('alertDestinationForm'));
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(AlertConfiguration);
