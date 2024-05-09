// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import NavBar from './NavBar';
import {
  logout,
  logoutSuccess,
  logoutFailure,
  fetchCustomerRunTimeConfigs,
  fetchCustomerRunTimeConfigsResponse
} from '../../../actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    logoutProfile: () => {
      dispatch(logout()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(logoutFailure(response.payload));
        } else {
          localStorage.removeItem('authToken');
          dispatch(logoutSuccess());
        }
      });
    },
    fetchCustomerRunTimeConfigs: () => {
      return dispatch(fetchCustomerRunTimeConfigs(true)).then((response) =>
        dispatch(fetchCustomerRunTimeConfigsResponse(response.payload))
      );
    }
  };
};

const mapStateToProps = (state) => {
  const {
    featureFlags: { test, released }
  } = state;
  return {
    customer: state.customer,
    enableBackupv2: test.backupv2 || released.backupv2
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(NavBar);
