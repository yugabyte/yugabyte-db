// Copyright (c) YugaByte, Inc.

import LoginForm from '../../../components/common/forms/LoginForm';
import {login, loginSuccess, loginFailure } from '../../../actions/customers';
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

//For any field errors upon submission (i.e. not instant check)
const validateAndLogInCustomer = (values, dispatch) => {
  return new Promise((resolve, reject) => {
    dispatch(login(values))
    .then((response) => {
      let data = response.payload.data;
      //if any one of these exist, then there is a field error
      if(response.payload.status !== 200) {
        //let other components know of error by updating the redux` state
        dispatch(loginFailure(response.payload));
        if (typeof data.error === 'string') {
          reject({_error: data.error});
        }
        reject(data.error);
      } else {
        localStorage.setItem('customer_token', response.payload.data.authToken);
        localStorage.setItem('customer_id',response.payload.data.customerUUID);
        dispatch(loginSuccess(response.payload));
        resolve();
      }
    });
  });
};

const mapDispatchToProps = (dispatch) => {
  return {
    loginCustomer: validateAndLogInCustomer,
    resetMe: () =>{

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
