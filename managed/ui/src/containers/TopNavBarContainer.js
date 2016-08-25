// Copyright (c) YugaByte, Inc.

import TopNavBar from '../components/TopNavBar.js';
import { logout, logoutSuccess, logoutFailure } from '../actions/customers';
import { connect } from 'react-redux';

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
