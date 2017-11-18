// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Col } from 'react-bootstrap';
import {YBInputField, YBButton} from '../common/forms/fields';
import { Field } from 'redux-form';
import _ from 'lodash';

export default class CustomerProfile extends Component {
  constructor(props) {
    super(props);
    this.updateCustomerProfile = this.updateCustomerProfile.bind(this);
  }

  updateCustomerProfile(values) {
    this.props.updateCustomerDetails(values);
  }

  componentWillReceiveProps(nextProps) {
    if (!_.isEqual(this.props.customer, nextProps.customer)) {
      this.props.change("email", nextProps.customer.email);
      this.props.change("name", nextProps.customer.name);
    }
  }

  render() {
    const {handleSubmit, submitting, customerProfile} = this.props;
    let profileUpdateStatus = <span/>;
    if (customerProfile.data === "updated-success") {
      profileUpdateStatus = <span className="pull-right yb-success-color">Profile Updated Successfully</span>;
    } else if (customerProfile.error) {
      profileUpdateStatus = <span className="pull-right yb-fail-color">Profile Update Failed</span>;
    }
    return (
      <div className="bottom-bar-padding">
        <Col lg={8} lgOffset={2} sm={12}>
          <div className="panel panel-default login-panel">
            <div className="panel-heading">
              <h3 className="panel-title">Update Customer Profile {profileUpdateStatus}</h3>
            </div>
            <div className="panel-body">
              <form name="EditCustomerProfile" onSubmit={handleSubmit(this.props.updateCustomerDetails)}>
                <Field name="name" type="text" component={YBInputField} label="Full Name" placeHolder="Full Name"/>
                <Field name="email" type="text" component={YBInputField} isReadOnly={true} label="Email" placeHolder="Email Address"/>
                <Field name="password" type="password" component={YBInputField} label="Password" placeHolder="Enter New Password"/>
                <Field name="confirmPassword" type="password" component={YBInputField} label="Confirm Password" placeHolder="Confirm New Password"/>
                <YBButton btnText="Save" btnType="submit" disabled={submitting} btnClass="btn btn-orange pull-right"/>
              </form>
            </div>
          </div>
        </Col>
      </div>
    );
  }
}
