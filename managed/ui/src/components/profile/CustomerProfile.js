// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import {YBInputField, YBButton, YBToggle} from '../common/forms/fields';
import { Field } from 'redux-form';
import _ from 'lodash';
import { isDefinedNotNull } from 'utils/ObjectUtils';

// TODO set predefined defaults another way not to share defaults this way
const CHECK_INTERVAL_MS = 300000;
const STATUS_UPDATE_INTERVAL_MS = 43200000;

export default class CustomerProfile extends Component {
  constructor(props) {
    super(props);
    this.toggleSendAlertsToYb = this.toggleSendAlertsToYb.bind(this);
  }

  componentWillReceiveProps(nextProps) {
    if (!_.isEqual(this.props.customer, nextProps.customer)) {
      this.props.change("email", nextProps.customer.email);
      this.props.change("name", nextProps.customer.name);
      this.props.change("code", nextProps.customer.code);
      if (isDefinedNotNull(nextProps.customer.alertingData)) {
        this.props.change("alertingData.alertingEmail",
            nextProps.customer.alertingData.alertingEmail);
        this.props.change("alertingData.sendAlertsToYb",
            nextProps.customer.alertingData.sendAlertsToYb);

        this.props.change("alertingData.checkIntervalMs",
            nextProps.customer.alertingData.checkIntervalMs || CHECK_INTERVAL_MS);

        this.props.change("alertingData.statusUpdateIntervalMs",
            nextProps.customer.alertingData.statusUpdateIntervalMs || STATUS_UPDATE_INTERVAL_MS);
      }
    }
  }

  toggleSendAlertsToYb(event) {
    this.setState({"alertingData.sendAlertsToYb": event.target.checked});
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
              <Field name="code" type="text" component={YBInputField} isReadOnly={true} label="Environment" placeHolder="Customer Code"/>
            </Col>
            <Col md={6} sm={12}>
              <h3>Change Password</h3>
              <Field name="password" type="password" component={YBInputField} label="Password" placeHolder="Enter New Password"/>
              <Field name="confirmPassword" type="password" component={YBInputField} label="Confirm Password" placeHolder="Confirm New Password"/>
            </Col>
          </Row>
          <Row>
            <Col md={6} sm={12}>
              <h3>Alerting controls</h3>
              <Field name="alertingData.alertingEmail" type="text" component={YBInputField} label="Alert email" placeHolder="Email to forward alerts to"/>
              <Field name="alertingData.sendAlertsToYb"
                     component={YBToggle} isReadOnly={false}
                     onToggle={this.toggleSendAlertsToYb}
                     label="Send alert emails to YugaByte team"
                     subLabel="Whether or not to send alerting emails to the YugaByte team."/>
              <Field name="alertingData.checkIntervalMs" type="text" component={YBInputField} label="Health check interval" placeHolder="Miliseconds to check universe status"/>
              <Field name="alertingData.statusUpdateIntervalMs" type="text" component={YBInputField} label="Report email interval" placeHolder="Miliseconds to send a status report email"/>
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
