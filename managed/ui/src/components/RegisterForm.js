// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

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
    const {asyncValidating, fields: { name, email, password, confirmPassword }, handleSubmit, submitting } = this.props;
    return (
      <div className="container">
        <div className="col-sm-6 col-sm-offset-3">
          <div className="panel panel-default login-panel">
            <div className="panel-heading">
              <h3 className="panel-title">Register Customer</h3>
            </div>
            <div className="panel-body">
              <form onSubmit={handleSubmit(this.props.registerCustomer.bind(this))}>
                <div className={`form-group ${name.touched && name.invalid ? 'has-error' : ''}`}>
                  <label className="control-label">Full Name*</label>
                  <input type="text" className="form-control" {...name} />
                  <div className="help-block">
                    {name.touched ? name.error : ''}
                  </div>
                </div>
                <div className={`form-group ${email.touched && email.invalid ? 'has-error' : ''}`}>
                  <label className="control-label">Email*</label>
                  <input type="email" className="form-control" {...email} />
                  <div className="help-block">
                    {email.touched ? email.error : ''}
                  </div>
                  <div className="help-block">
                    {asyncValidating === 'email' ? 'validating..': ''}
                  </div>
                </div>
                <div className={`form-group ${password.touched && password.invalid ? 'has-error' : ''}`}>
                  <label className="control-label">Password*</label>
                  <input type="password" className="form-control" {...password} />
                  <div className="help-block">
                    {password.touched ? password.error : ''}
                  </div>
                </div>
                <div className={`form-group ${confirmPassword.touched && confirmPassword.invalid ? 'has-error' : ''}`}>
                  <label className="control-label">Confirm Password*</label>
                  <input type="password" className="form-control" {...confirmPassword} />
                  <div className="help-block">
                    {confirmPassword.touched ? confirmPassword.error : ''}
                  </div>
                </div>
                <button type="submit" className="btn btn-lg btn-success btn-block"  disabled={submitting} >Submit</button>
              </form>
            </div>
          </div>
        </div>
      </div>
    );
  }
}

export default RegisterForm;
