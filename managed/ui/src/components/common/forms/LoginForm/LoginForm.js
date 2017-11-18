// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { PageHeader } from 'react-bootstrap';
import { Field } from 'redux-form';
import { YBButton, YBInputField } from '../fields';
import {getPromiseState} from 'utils/PromiseUtils';
import YBLogo from '../../YBLogo/YBLogo';
import {browserHistory} from 'react-router';

class LoginForm extends Component {
  constructor(props) {
    super(props);
    this.submitLogin = this.submitLogin.bind(this);
  }

  submitLogin(formValues) {
    const {loginCustomer} = this.props;
    loginCustomer(formValues);
  }

  componentWillReceiveProps(nextProps) {
    const {customer: {authToken}} =  nextProps;
    if (getPromiseState(authToken).isSuccess()) {
      browserHistory.push('/');
    }
  }

  render() {
    const { handleSubmit, submitting, customer: {authToken} } = this.props;
    return (
      <div className="container full-height dark-background flex-vertical-middle">
        <div className="col-sm-5 dark-form">
          <PageHeader bsClass="dark-form-heading">
            <YBLogo type="full"/>
            <span>Admin Console</span>
          </PageHeader>
          <form onSubmit={handleSubmit(this.submitLogin)}>
            <div className={`alert alert-danger form-error-alert ${authToken.error ? '': 'hide'}`}>
              {<strong>{JSON.stringify(authToken.error)}</strong>}
            </div>

            <div className="clearfix">
              <Field name="email" placeholder="Enter email" type="text" component={YBInputField} label="Username" />
              <Field name="password" placeholder="Your password" type="password" component={YBInputField} label="Password" />
            </div>
            <div className="clearfix">
              <YBButton btnType="submit" btnDisabled={submitting || getPromiseState(authToken).isLoading()}
                        btnClass="btn btn-orange" btnText="Login"/>
            </div>
          </form>
        </div>
      </div>
    );
  }
}

export default LoginForm;
