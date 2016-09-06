// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Field, reduxForm } from 'redux-form';
import YBInputField from './YBInputField';

class RegisterForm extends Component {
  static contextTypes = {
    router: PropTypes.object
  };

  componentWillMount() {
    this.props.resetMe();
  }

  componentWillReceiveProps(nextProps) {
    if(nextProps.customer.status === 'authenticated' && nextProps.customer.customer && !nextProps.customer.error) {
      this.context.router.push('/');
    }
  }

  render() {
    const { handleSubmit, submitting } = this.props;
    return (
      <div className="container">
        <div className="col-sm-6 col-sm-offset-3">
          <div className="panel panel-default login-panel">
            <div className="panel-heading">
              <h3 className="panel-title">Register Customer</h3>
            </div>
            <div className="panel-body">
              <div className={`alert alert-danger form-error-alert
                ${this.props.customer.error ? '': 'hide'}`}>
                  {<strong>{this.props.customer.error}</strong>}
              </div>
              <form onSubmit={handleSubmit(this.props.registerCustomer.bind(this))}>
                <Field name="name" type="text" component={YBInputField} label="Full Name"/>
                <Field name="email" type="email" component={YBInputField} label="Email"/>
                <Field name="password" type="password" component={YBInputField} label="Password"/>
                <Field name="confirmPassword" type="password" component={YBInputField} label="Confirm Password"/>
                <button type="submit" className="btn btn-lg btn-success btn-block"
                        disabled={submitting} >Submit</button>
              </form>
            </div>
          </div>
        </div>
      </div>
    );
  }
}

export default RegisterForm;
