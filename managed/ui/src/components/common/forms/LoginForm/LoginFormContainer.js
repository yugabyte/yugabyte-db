// Copyright (c) YugaByte, Inc.

import LoginForm from './LoginForm';
import { login, loginResponse, resetCustomerError }
  from '../../../../actions/customers';
import { connect } from 'react-redux';

const mapDispatchToProps = (dispatch) => {
  return {
    loginCustomer: (formValues) => {
      dispatch(login(formValues)).then((response) => {
        if (response.payload.status === 200) {
          localStorage.setItem('authToken', response.payload.data.authToken);
          localStorage.setItem('customerId',response.payload.data.customerUUID);
        }
        dispatch(loginResponse(response.payload));
      });
    },
    resetCustomerError: () => dispatch(resetCustomerError()),
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(LoginForm);
