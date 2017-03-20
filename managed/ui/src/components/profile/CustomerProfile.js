// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Col } from 'react-bootstrap';
import {YBInputField, YBButton} from '../common/forms/fields'
import { Field } from 'redux-form';

export default class CustomerProfile extends Component {
  constructor(props) {
    super(props);
    this.updateCustomerProfile = this.updateCustomerProfile.bind(this);
  }

  updateCustomerProfile(values) {
    this.props.updateCustomerDetails(values);
  }

  render() {
    const {handleSubmit, submitting} = this.props;
    return (
      <div className="bottom-bar-padding">
        <Col lg={8} lgOffset={2} sm={12}>
          <div className="panel panel-default login-panel">
            <div className="panel-heading">
              <h3 className="panel-title">Update Customer Profile</h3>
            </div>
            <div className="panel-body">
              <div className={`alert alert-danger form-error-alert
              ${this.props.customer.error ? '': 'hide'}`}>
                {<strong>{this.props.customer.error}</strong>}
              </div>
              <form name="EditCustomerProfile" onSubmit={handleSubmit(this.updateCustomerProfile)}>
                <Field name="name" type="text" component={YBInputField} label="Full Name" placeHolder="Full Name"/>
                <Field name="email" type="email" component={YBInputField} isReadOnly={true} label="Email" placeHolder="Email Address"/>
                <Field name="password" type="password" component={YBInputField} label="Password" placeHolder="Enter New Password"/>
                <Field name="confirmPassword" type="password" component={YBInputField} label="Confirm Password" placeHolder="Confirm New Password"/>
                <YBButton btnText="Save" btnType="submit" disabled={submitting} btnClass="btn btn-default bg-orange pull-right"/>
              </form>
            </div>
          </div>
        </Col>
      </div>
    )
  }
}
