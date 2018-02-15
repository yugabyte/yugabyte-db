// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { PageHeader } from 'react-bootstrap';
import { Field } from 'redux-form';
import { YBButton, YBInputField } from '../fields';
import YBLogo from '../../YBLogo/YBLogo';
import {browserHistory} from 'react-router';
import {getPromiseState} from 'utils/PromiseUtils';

class RegisterForm extends Component {
  componentWillReceiveProps(nextProps) {
    const {customer: {authToken}} =  nextProps;
    if (getPromiseState(authToken).isSuccess()) {
      browserHistory.push('/');
    }
  }

  submitRegister = formValues => {
    const {registerCustomer} = this.props;
    registerCustomer(formValues);
  };

  render() {
    const { handleSubmit, submitting, customer: {authToken} } = this.props;

    return (
      <div className="container full-height dark-background flex-vertical-middle">
        <div className="col-sm-6 dark-form">
          <PageHeader bsClass="dark-form-heading">
            <YBLogo />
            <span>Admin Console Registration</span>
          </PageHeader>
          <form onSubmit={handleSubmit(this.props.registerCustomer.bind(this))}>
            <div className={`alert alert-danger form-error-alert ${authToken.error ? '': 'hide'}`}>
              {<strong>{JSON.stringify(authToken.error)}</strong>}
            </div>
            <div className="form-right-aligned-labels">
              <Field name="code" type="text" component={YBInputField} label="Code"/>
              <Field name="name" type="text" component={YBInputField} label="Full Name"/>
              <Field name="email" type="email" component={YBInputField} label="Email"/>
              <Field name="password" type="password" component={YBInputField} label="Password"/>
              <Field name="confirmPassword" type="password" component={YBInputField} label="Confirm Password"/>
            </div>
            <div className="clearfix">
              <YBButton btnType="submit" btnDisabled={submitting || getPromiseState(authToken).isLoading()}
                        btnClass="btn btn-orange pull-right" btnText="Register"/>
            </div>
          </form>
        </div>
      </div>
    );
  }
}

export default RegisterForm;
