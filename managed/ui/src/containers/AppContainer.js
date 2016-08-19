// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { validateToken, validateTokenSuccess, validateTokenFailure, resetToken } from '../actions/customers';
import App from '../components/App.js';

const mapDispatchToProps = (dispatch) => {
  return {
    loadCustomerFromToken: () => {
      let token = localStorage.getItem('customer_token');
      if(!token || token === '') {
        return;
      }
      dispatch(validateToken(token))
      .then((response) => {
        if (!response.error) {
          dispatch(validateTokenSuccess(response.payload))
        } else {
          localStorage.removeItem('customer_token');//remove token from storage
          dispatch(validateTokenFailure(response.payload));
        }
      });
    },
    resetMe: () =>{
      localStorage.removeItem('customer_token'); //remove token from storage
      dispatch(resetToken());
    }
  }
}

export default connect(null, mapDispatchToProps)(App);
