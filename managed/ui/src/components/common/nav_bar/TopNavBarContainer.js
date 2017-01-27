// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import TopNavBar from './TopNavBar';
import { logout, logoutSuccess, logoutFailure } from '../../../actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    logoutProfile: () => {
      dispatch(logout())
      .then((response) => {
        if (response.payload.status !== 200) {
          dispatch(logoutFailure(response.payload));
        } else {
          localStorage.removeItem('customer_token');
          dispatch(logoutSuccess());
        }
      });
    }
  }
}

export default connect(null, mapDispatchToProps)(TopNavBar);
