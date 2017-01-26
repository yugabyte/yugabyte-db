// Copyright (c) YugaByte, Inc.

import RegisterForm from '../../../components/common/forms/RegisterForm';
import {register, registerSuccess, registerFailure } from '../../../actions/customers';
import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';

//Client side validation
function validate(values) {
  var errors = {};
  var hasErrors = false;

  if (!values.name || values.name.trim() === '') {
    errors.name = 'Enter a name';
    hasErrors = true;
  }

  if(!values.email || values.email.trim() === '') {
    errors.email = 'Enter email';
    hasErrors = true;
  }
  if(!values.password || values.password.trim() === '') {
    errors.password = 'Enter password';
    hasErrors = true;
  }
  if(!values.confirmPassword || values.confirmPassword.trim() === '') {
    errors.confirmPassword = 'Enter Confirm Password';
    hasErrors = true;
  }

  if(values.confirmPassword  && values.confirmPassword.trim() !== '' &&
  values.password  && values.password.trim() !== '' &&
  values.password !== values.confirmPassword) {
    errors.password = 'Password And Confirm Password don\'t match';
    errors.password = 'Password And Confirm Password don\'t match';
    hasErrors = true;
  }
  return hasErrors && errors;
}

const mapDispatchToProps = (dispatch) => {
  return {
    registerCustomer: (formVals)=> {
      dispatch(register(formVals)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(registerFailure(response.payload));
        } else {
          localStorage.setItem('customer_token', response.payload.data.authToken);
          localStorage.setItem('customer_id',response.payload.data.customerUUID);
          dispatch(registerSuccess(response.payload));
        }
      });
    }
  }
}


function mapStateToProps(state, ownProps) {
  return {
    customer: state.customer,
    validateFields: state.validateFields
  };
}

var registerForm = reduxForm({
  form: 'RegisterForm',
  fields: ['name', 'email', 'password', 'confirmPassword'],
  validate
});

module.exports = connect(mapStateToProps, mapDispatchToProps)(registerForm(RegisterForm));
