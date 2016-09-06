// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Field, reduxForm } from 'redux-form';
import  YBInputField  from './YBInputField';

class LoginForm extends Component {
  static contextTypes = {
    router: PropTypes.object
  };

  componentWillReceiveProps(nextProps) {
    if (nextProps.customer.status === 'authenticated' &&
        nextProps.customer.customer &&
        !nextProps.customer.error) {
        this.context.router.push('/home');
    }
  }

  render() {
    const { handleSubmit, submitting } = this.props;
    return (
      <div className="container">
        <div className="col-sm-6 col-sm-offset-3">
          <div className="panel panel-default login-panel">
            <div className="panel-heading">
              <h3 className="panel-title">Please log in</h3>
            </div>
            <div className="panel-body">
              <div className={`alert alert-danger form-error-alert
                ${this.props.customer.error ? '': 'hide'}`}>
                  {<strong>{this.props.customer.error}</strong>}
              </div>
              <form onSubmit={handleSubmit(this.props.loginCustomer.bind(this))}>
                <Field name="email" type="email" component={YBInputField} label="Email"/>
                <Field name="password" type="password" component={YBInputField} label="Password"/>
                <div>
                  <button type="submit" className="btn btn-lg btn-success btn-block"
                          disabled={submitting} >Submit</button>
                </div>
              </form>
            </div>
            </div>
          </div>
        </div>
    );
  }
}

export default LoginForm;
