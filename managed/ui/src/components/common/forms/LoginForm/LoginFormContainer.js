// Copyright (c) YugaByte, Inc.

import LoginForm from './LoginForm';
import { login, loginResponse } from '../../../../actions/customers';
import { connect } from 'react-redux';

const mapDispatchToProps = (dispatch) => {
  return {
    loginCustomer: (formValues) => {
      dispatch(login(formValues)).then((response) => {
        if (response.payload.status === 200) {
          localStorage.setItem('customer_token', response.payload.data.authToken);
          localStorage.setItem('customer_id',response.payload.data.customerUUID);
        }
        dispatch(loginResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(LoginForm);
