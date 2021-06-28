// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nissharma@deloitte.com)
//
// TODO: Redux still needs to be configured once the API contract
// will be finalized and be available.

import { connect } from 'react-redux';
import { alertConfigs } from '../../../actions/customers';
import { CustomerProfile } from '../../profile';
import { AlertConfiguration } from './AlertConfiguration';

const mapStateToProps = (state) => {
  return {
    customer: state.customer.currentCustomer,
    users: state.customer.users.data,
    apiToken: state.customer.apiToken,
    customerProfile: state.customer ? state.customer.profile : null
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    alertConfigs: () => {
      return dispatch(alertConfigs());
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(AlertConfiguration, CustomerProfile);
