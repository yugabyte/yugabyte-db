// Copyright (c) YugaByte, Inc.

import RegisterForm from '../components/RegisterForm.js';
import {register, registerSuccess, registerFailure } from '../actions/customers';
import { reduxForm } from 'redux-form';

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

const validateAndRegisterCustomer = (values, dispatch) => {
  return new Promise((resolve, reject) => {
        dispatch(register(values)).then((response) => {
        let data = response.payload.data;
        if(response.payload.status !== 200) {
            dispatch(registerFailure(response.payload));
            reject(data); //this is for redux-form itself
         } else {
            localStorage.setItem('customer_token', response.payload.data.authToken);
            localStorage.setItem('customer_id',response.payload.data.customerUUID);
            dispatch(registerSuccess(response.payload));
            resolve();
        }
      });
  });
};

const mapDispatchToProps = (dispatch) => {
  return {
   registerCustomer: validateAndRegisterCustomer,
   resetMe: () =>{

    }
  }
}


function mapStateToProps(state, ownProps) {
  return {
    customer: state.customer,
    validateFields: state.validateFields
  };
}


// connect: first argument is mapStateToProps, 2nd is mapDispatchToProps
// reduxForm: 1st is form config, 2nd is mapStateToProps, 3rd is mapDispatchToProps
export default reduxForm({
  form: 'RegisterForm',
  fields: ['name', 'email', 'password', 'confirmPassword'],
  validate
}, mapStateToProps, mapDispatchToProps)(RegisterForm);
