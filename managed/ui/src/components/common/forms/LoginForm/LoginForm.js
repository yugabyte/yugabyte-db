// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { PageHeader } from 'react-bootstrap';
import { Field } from 'redux-form';

import { YBInputField } from '../fields';
import YBLogo from '../../YBLogo/YBLogo';

import './LoginForm.scss';

class LoginForm extends Component {
  constructor(props) {
    super(props);
    this.submitLogin = this.submitLogin.bind(this);
  }
  static contextTypes = {
    router: PropTypes.object
  };
  
  submitLogin(formValues) {
    const {loginCustomer} = this.props;
    loginCustomer(formValues);
  }

  componentWillReceiveProps(nextProps) {
    if (nextProps.customer.status === 'authenticated' &&
        nextProps.customer.customer &&
        !nextProps.customer.error) {
        this.context.router.push('/');
    }
  }

  render() {
    const { handleSubmit, submitting } = this.props;
    return (
      <div className="container full-height dark-background flex-vertical-middle">
        <div className="col-sm-5 login-form">
          <PageHeader bsClass="login-heading">
            <YBLogo />
            <span>Administrative Console</span>
          </PageHeader>
          <form onSubmit={handleSubmit(this.submitLogin)}>
            <div className={`alert alert-danger form-error-alert
              ${this.props.customer.error ? '': 'hide'}`}>
                {<strong>{this.props.customer.error}</strong>}
            </div>

            <div className="form-right-aligned-labels">
              <Field name="email" type="email" component={YBInputField} label="Email" />
              <Field name="password" type="password" component={YBInputField} label="Password" />
            </div>
            <div className="clearfix">
              <button type="submit" className="btn btn-default bg-orange pull-right"
                        disabled={submitting} >Login</button>
            </div>
          </form>
        </div>
      </div>
    );
  }
}

export default LoginForm;
