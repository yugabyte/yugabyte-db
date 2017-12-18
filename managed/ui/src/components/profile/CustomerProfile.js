// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
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
        <form name="EditCustomerProfile" onSubmit={handleSubmit(this.props.updateCustomerDetails)}>
          <Row>
            <h2 className="content-title">Update Customer Profile {profileUpdateStatus}</h2>
            <Col md={6} sm={12}>
              <h3>Profile Info</h3>
              <Field name="name" type="text" component={YBInputField} label="Full Name" placeHolder="Full Name"/>
              <Field name="email" type="text" component={YBInputField} isReadOnly={true} label="Email" placeHolder="Email Address"/>
            </Col>
            <Col md={6} sm={12}>
              <h3>Change Password</h3>
              <Field name="password" type="password" component={YBInputField} label="Password" placeHolder="Enter New Password"/>
              <Field name="confirmPassword" type="password" component={YBInputField} label="Confirm Password" placeHolder="Confirm New Password"/>
            </Col>
          </Row>
          <Row>
            <Col sm={12}>
              <YBButton btnText="Save All Changes" btnType="submit" disabled={submitting} btnClass="btn btn-orange pull-right"/>
            </Col>
          </Row>
        </form>
      </div>
    );
  }
}
