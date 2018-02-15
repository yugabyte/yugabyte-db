// Copyright (c) YugaByte, Inc.

import RegisterForm from './RegisterForm';
import {register, registerResponse } from '../../../../actions/customers';
import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';

//Client side validation
function validate(values) {
  const errors = {};
  let hasErrors = false;
  if (!values.code || values.code.trim() === '') {
    errors.code = 'Enter a code';
    hasErrors = true;
  } else if (values.code.length > 5) {
    errors.code = 'Code can be only 5 characters long';
    hasErrors = true;
  }

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
    errors.password = 'Password and Confirm Password don\'t match';
    errors.password = 'Password and Confirm Password don\'t match';
    hasErrors = true;
  }
  return hasErrors && errors;
}

const mapDispatchToProps = (dispatch) => {
  return {
    registerCustomer: (formVals)=> {
      dispatch(register(formVals)).then((response) => {
        if (response.payload.status === 200) {
          localStorage.setItem('customer_token', response.payload.data.authToken);
          localStorage.setItem('customer_id', response.payload.data.customerUUID);
        }
        dispatch(registerResponse(response.payload));
      });
    }
  };
};


function mapStateToProps(state) {
  return {
    customer: state.customer,
    validateFields: state.validateFields
  };
}

const registerForm = reduxForm({
  form: 'RegisterForm',
  fields: ['code', 'name', 'email', 'password', 'confirmPassword'],
  validate
});

export default connect(mapStateToProps, mapDispatchToProps)(registerForm(RegisterForm));
