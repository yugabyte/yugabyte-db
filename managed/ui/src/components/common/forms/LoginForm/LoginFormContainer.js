// Copyright (c) YugaByte, Inc.

import LoginForm from './LoginForm';
import {login, loginSuccess, loginFailure } from '../../../../actions/customers';
import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';

//Client side validation
function validate(values) {
  var errors = {};
  var hasErrors = false;
  if (!values.email || values.email.trim() === '') {
    errors.email = 'Enter email';
    hasErrors = true;
  }
  if(!values.password || values.password.trim() === '') {
    errors.password = 'Enter password';
    hasErrors = true;
  }
  return hasErrors && errors;
}


const mapDispatchToProps = (dispatch) => {
  return {
    loginCustomer: (formValues)=> {
      dispatch(login(formValues))
        .then((response) => {
          if(response.payload.status !== 200) {
            dispatch(loginFailure(response.payload));
          } else {
            localStorage.setItem('customer_token', response.payload.data.authToken);
            localStorage.setItem('customer_id',response.payload.data.customerUUID);
            dispatch(loginSuccess(response.payload));
          }
        });
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    customer: state.customer
  };
}


var loginForm = reduxForm({
  form: 'LoginForm',
  fields: ['email', 'password'],
  validate
})


module.exports = connect(mapStateToProps, mapDispatchToProps)(loginForm(LoginForm));
