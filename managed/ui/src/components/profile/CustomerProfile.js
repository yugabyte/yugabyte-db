// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import {YBFormInput, YBButton, YBToggle, YBControlledSelectWithLabel} from '../common/forms/fields';
import { Formik, Form, Field } from 'formik';
import { browserHistory} from 'react-router';
import { showOrRedirect, isNonAvailable } from 'utils/LayoutUtils';
import _ from 'lodash';
import * as Yup from 'yup';

// TODO set predefined defaults another way not to share defaults this way
const CHECK_INTERVAL_MS = 300000;
const STATUS_UPDATE_INTERVAL_MS = 43200000;

export default class CustomerProfile extends Component {

  componentWillMount() {
    const { customer } = this.props;
    if (isNonAvailable(customer.features, "main.profile")) browserHistory.push('/');
  }

  render() {
    const { customer = {}, customerProfile, updateCustomerDetails } = this.props;

    showOrRedirect(customer.features, "main.profile");

    let profileUpdateStatus = <span/>;
    if (customerProfile.data === "updated-success") {
      profileUpdateStatus = <span className="pull-right yb-success-color">Profile Updated Successfully</span>;
    } else if (customerProfile.error) {
      profileUpdateStatus = <span className="pull-right yb-fail-color">Profile Update Failed</span>;
    }

    const validationSchema = Yup.object().shape({
      name: Yup.string()
        .required('Enter name'),
    
      email: Yup.string()
        .required('Enter email')
        .email('This is not a valid email'),  

      code: Yup.string()
        .required('Enter Environment name')
        .max(5, 'Environment name can be only 5 characters long'),

      password: Yup.string()
        .notRequired()
        .oneOf([Yup.ref('confirmPassword')], "Passwords don't match"),

      confirmPassword: Yup.string()
        .notRequired()
        .oneOf([Yup.ref('password')], "Passwords don't match"),

      callhomeLevel: Yup.string(),

      sendAlertsToYb: Yup.boolean(),

      alertingEmail: Yup.string().nullable(),

      checkIntervalMs: Yup.number()
        .typeError('Must specify a number')
        .integer(),

      statusUpdateIntervalMs: Yup.number()
        .typeError('Must specify a number')
        .integer(),
    });

    const alertingDataProps = ['alertingEmail', 'checkIntervalMs', 'statusUpdateIntervalMs', 'sendAlertsToYb'];
    
    const initialValues = {
      name: customer.name || '',
      email: customer.email || '',
      code: customer.code || '',
      
      // alertingData properties
      alertingEmail: customer.alertingData ?
        customer.alertingData.alertingEmail || '':
        '',
      checkIntervalMs: customer.alertingData ?
        customer.alertingData.checkIntervalMs :
        CHECK_INTERVAL_MS,
      statusUpdateIntervalMs: customer.alertingData ?
        customer.alertingData.statusUpdateIntervalMs :
        STATUS_UPDATE_INTERVAL_MS,
      sendAlertsToYb: customer.alertingData && customer.alertingData.sendAlertsToYb,
      
      password: '',
      callhomeLevel: customer.callhomeLevel || 'NONE',
      confirmPassword: '',
    };

    const callhomeOptions = [
      <option value="NONE" key={0}>None</option>,
      <option value="LOW" key={1}>Low</option>,
      <option value="MEDIUM" key={2}>Medium</option>,
      <option value="HIGH" key={3}>High</option>
    ];

    return (
      <div className="bottom-bar-padding">
        <Formik
          validationSchema={validationSchema}
          initialValues={initialValues}
          enableReinitialize
          onSubmit={(values, { setSubmitting }) => {
            /**
             * Generate object to be sent with only changes from the `initialValues` object.
             * Properties in `alertingDataProps` will be moved under `alertingData` key.
             */
            const removedDefaults = _.omitBy(values, (v, k) => v === initialValues[k]);

            // Required fields for API
            const alertingObj = {
              name: values.name,
              email: values.email,
              code: values.code,
            };
            const omitProps = _.pick(removedDefaults, alertingDataProps);
            if (!_.isEmpty(omitProps)) {
              alertingObj.alertingData = omitProps;
            }

            const diffResult = _.assign(_.omit(removedDefaults, alertingDataProps), alertingObj);
            updateCustomerDetails(diffResult);
            setSubmitting(false);
          }}
          render={({
            values,
            handleChange,
            handleSubmit,
            isSubmitting,
          }) => (
            <Form name="EditCustomerProfile" onSubmit={handleSubmit}>
              <Row>
                <h2 className="content-title">Update Customer Profile {profileUpdateStatus}</h2>
                <Col md={6} sm={12}>
                  <h3>Profile Info</h3>
                  <Field name="name" type="text" component={YBFormInput} placeholder="Full Name" label="Full Name"/>                
                  <Field name="email" readOnly={true} type="text" label="Email" component={YBFormInput} placeholder="Email Address" />
                  <Field name="code" readOnly={true} type="text" label="Environment" component={YBFormInput} placeholder="Customer Code" />
                </Col>
                <Col md={6} sm={12}>
                  <h3>Change Password</h3>
                  <Field name="password" type="password" component={YBFormInput} label="Password" placeholder="Enter New Password"/>
                  <Field name="confirmPassword" type="password" component={YBFormInput} label="Confirm Password" placeholder="Confirm New Password"/>
                </Col>
              </Row>
              <Row>
                <Col md={6} sm={12}>
                  <h3>Alerting controls</h3>
                  <Field name="alertingEmail" type="text" component={YBFormInput} label="Alert email" placeholder="Email to forward alerts to"/>
                  <Field render={({field, form}) => (
                    <YBToggle onToggle={handleChange}
                      name="sendAlertsToYb"
                      input={{ 
                        value: field.value.sendAlertsToYb,
                        onChange: field.onChange,
                      }}
                      label="Send alert emails to YugaByte team"
                      subLabel="Whether or not to send alerting emails to the YugaByte team."
                    />
                  )}
                  />
                  <Field name="callhomeLevel" 
                    component={YBControlledSelectWithLabel}
                    label="Callhome Level"
                    input={{name: 'callhomeLevel'}}
                    onInputChanged={handleChange}
                    selectVal={values.callhomeLevel}
                    options={callhomeOptions}
                  />
                  <Field name="checkIntervalMs" type="text" component={YBFormInput} label="Health check interval" placeholder="Miliseconds to check universe status"/>
                  <Field name="statusUpdateIntervalMs" type="text" component={YBFormInput} label="Report email interval" placeholder="Miliseconds to send a status report email"/>
                </Col>
              </Row>
              <Row>
                <Col sm={12}>
                  <YBButton btnText="Save All Changes" 
                    btnType="submit" 
                    disabled={isSubmitting} 
                    btnClass="btn btn-orange pull-right" 
                  />
                </Col>
              </Row>
            </Form>
          )}
        />
      </div>
    );
  }
}
