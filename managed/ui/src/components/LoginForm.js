// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

class LoginForm extends Component {
  static contextTypes = {
    router: PropTypes.object
  };

  componentWillMount() {
     this.props.resetMe();
  }

  componentWillReceiveProps(nextProps) {
    if(nextProps.customer.status === 'authenticated' && nextProps.customer.customer && !nextProps.customer.error) {
      this.context.router.push('/home');
    }
    if(nextProps.customer.status === 'signin' && !nextProps.customer.customer && nextProps.customer.error && !this.props.customer.error) {
      alert(nextProps.customer.error.message);
    }
  }
  render() {
    const {asyncValidating, fields: {email, password}, handleSubmit, submitting } = this.props;
    return (
        <div className="container">
          <div className="col-sm-6 col-sm-offset-3">
            <div className="panel panel-default login-panel">
              <div className="panel-heading">
                <h3 className="panel-title">Please log in</h3>
              </div>
              <div className="panel-body">
                <form onSubmit={handleSubmit(this.props.loginCustomer.bind(this))}>
                  <div className={`form-group ${email.touched && email.invalid ? 'has-error' : ''}`}>
                    <label className="control-label">Email</label>
                    <input  placeholder="email" type="text" className="form-control" {...email} />
                    <div className="help-block">
                    {email.touched ? email.error : ''}
                    </div>
                    <div className="help-block">
                    {asyncValidating === 'email' ? 'validating..': ''}
                    </div>
                  </div>
                  <div className={`form-group ${password.touched && password.invalid ? 'has-error' : ''}`}>
                    <label className="control-label">Password</label>
                    <input placeholder="password" type="password" className="form-control" {...password} />
                    <div className="help-block">
                    {password.touched ? password.error : ''}
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

export default LoginForm;
