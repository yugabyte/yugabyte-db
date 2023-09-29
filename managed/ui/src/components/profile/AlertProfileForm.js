// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import {
  YBFormInput,
  YBButton,
  YBToggle,
  YBControlledSelectWithLabel
} from '../common/forms/fields';
import { Formik, Form, Field } from 'formik';
import { isDisabled, showOrRedirect } from '../../utils/LayoutUtils';
import * as Yup from 'yup';
import _ from 'lodash';
import { isNonEmptyObject, isNonEmptyArray } from '../../utils/ObjectUtils';
import { getPromiseState } from '../../utils/PromiseUtils';
import { RbacValidator } from '../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../redesign/features/rbac/UserPermPathMapping';

// TODO set predefined defaults another way not to share defaults this way
const MILLISECONDS_IN_MINUTE = 60000;
const DEFAULT_CHECK_INTERVAL_MINUTES = 5;
const DEFAULT_STATUS_UPDATE_INTERVAL_MINUTES = 720;
const DEFAULT_ACTIVE_ALERT_NOTIFICATION_INTERVAL_MINUTES = 0;
const DEFAULT_SMTP_PORT = 587;

const validationSchema = Yup.object().shape({
  emailNotifications: Yup.boolean(),
  alertingData: Yup.object().when('emailNotifications', {
    is: true,
    then: Yup.object({
      sendAlertsToYb: Yup.boolean().default(false).nullable(),
      alertingEmail: Yup.string().required('Must specify at least one email address'),
      checkIntervalMs: Yup.number().typeError('Must specify a number'),
      statusUpdateIntervalMs: Yup.number().typeError('Must specify a number'),
      activeAlertNotificationIntervalMs: Yup.number().typeError('Must specify a number'),
      reportOnlyErrors: Yup.boolean().default(false).nullable()
    }),
    otherwise: Yup.object({
      sendAlertsToYb: Yup.boolean().default(false).nullable(),
      alertingEmail: Yup.string().nullable(),
      checkIntervalMs: Yup.number().typeError('Must specify a number'),
      statusUpdateIntervalMs: Yup.number().typeError('Must specify a number'),
      activeAlertNotificationIntervalMs: Yup.number().typeError('Must specify a number'),
      reportOnlyErrors: Yup.boolean().default(false).nullable()
    })
  }),
  smtpData: Yup.object().when('emailNotifications', {
    is: true,
    then: Yup.object({
      smtpServer: Yup.string().required('Must specify an SMTP server address'),
      smtpPort: Yup.number().typeError('Must specify an SMTP server port number'),
      emailFrom: Yup.string().email('Must be an email'),
      smtpUsername: Yup.string(),
      smtpPassword: Yup.string(),
      useSSL: Yup.boolean(),
      useTLS: Yup.boolean()
    })
  }),
  callhomeLevel: Yup.string()
});

const callhomeOptions = [
  <option value="NONE" key={0}>
    None
  </option>,
  <option value="LOW" key={1}>
    Low
  </option>,
  <option value="MEDIUM" key={2}>
    Medium
  </option>,
  <option value="HIGH" key={3}>
    High
  </option>
];

export default class AlertProfileForm extends Component {
  constructor(props) {
    super(props);
    this.state = {
      statusUpdated: false
    };
  }

  componentDidUpdate() {
    const { customerProfile } = this.props;
    const { statusUpdated } = this.state;

    if (
      statusUpdated &&
      (getPromiseState(customerProfile).isSuccess() || getPromiseState(customerProfile).isError())
    ) {
      this.setState({
        statusUpdated: false
      });
    }
  }

  render() {
    const { customer = {}, users = [], updateCustomerDetails } = this.props;

    showOrRedirect(customer.data.features, 'main.profile');
    const isReadOnly = isDisabled(customer.data.features, 'health.configure');

    // Filter users for userUUID set during login
    const loginUserId = localStorage.getItem('userId');
    const getCurrentUser = isNonEmptyArray(users)
      ? users.filter((u) => u.uuid === loginUserId)
      : [];
    const initialValues = {
      name: customer.data.name || '',
      email: (getCurrentUser.length && getCurrentUser[0].email) || '',
      code: customer.data.code || '',

      alertingData: {
        alertingEmail: customer.data.alertingData
          ? customer.data.alertingData.alertingEmail || ''
          : '',
        checkIntervalMinutes: getPromiseState(customer).isSuccess()
          ? isNonEmptyObject(customer.data.alertingData)
            ? customer.data.alertingData.checkIntervalMs / MILLISECONDS_IN_MINUTE
            : DEFAULT_CHECK_INTERVAL_MINUTES
          : '',
        statusUpdateIntervalMinutes: getPromiseState(customer).isSuccess()
          ? isNonEmptyObject(customer.data.alertingData)
            ? customer.data.alertingData.statusUpdateIntervalMs / MILLISECONDS_IN_MINUTE
            : DEFAULT_STATUS_UPDATE_INTERVAL_MINUTES
          : '',
        activeAlertNotificationIntervalMinutes: getPromiseState(customer).isSuccess()
          ? isNonEmptyObject(customer.data.alertingData)
            ? customer.data.alertingData.activeAlertNotificationIntervalMs / MILLISECONDS_IN_MINUTE
            : DEFAULT_ACTIVE_ALERT_NOTIFICATION_INTERVAL_MINUTES
          : '',
        sendAlertsToYb: customer.data.alertingData?.sendAlertsToYb,
        reportOnlyErrors: customer.data.alertingData?.reportOnlyErrors
      },
      emailNotifications: isNonEmptyObject(_.get(customer, 'data.smtpData', {})),
      smtpData: {
        smtpServer: _.get(customer, 'data.smtpData.smtpServer', ''),
        smtpPort: _.get(customer, 'data.smtpData.smtpPort', DEFAULT_SMTP_PORT),
        emailFrom: _.get(customer, 'data.smtpData.emailFrom', ''),
        // ensure username and password are always strings, even if API returns "null" as a value
        smtpUsername: _.get(customer, 'data.smtpData.smtpUsername') || '',
        smtpPassword: _.get(customer, 'data.smtpData.smtpPassword') || '',
        useSSL: _.get(customer, 'data.smtpData.useSSL', false),
        useTLS: _.get(customer, 'data.smtpData.useTLS', false)
      },
      callhomeLevel: customer.data.callhomeLevel || 'NONE'
    };

    return (
      <div className="bottom-bar-padding">
        <Formik
          validationSchema={validationSchema}
          initialValues={initialValues}
          enableReinitialize
          onSubmit={(values, { setSubmitting, resetForm }) => {
            const data = _.omit(values, 'emailNotifications'); // don't submit internal helper field
            if (values.emailNotifications) {
              // due to smtp specifics have to remove smtpUsername/smtpPassword props from payload when they are empty
              if (!data.smtpData.smtpUsername)
                data.smtpData = _.omit(data.smtpData, 'smtpUsername');
              if (!data.smtpData.smtpPassword)
                data.smtpData = _.omit(data.smtpData, 'smtpPassword');
            } else {
              data.alertingData.alertingEmail = ''; // this will clean alerting email
              data.smtpData = null; // this will revert smtp settings to default presets
            }

            // convert back from minutes to milliseconds and remove helper fields
            data.alertingData.checkIntervalMs =
              data.alertingData.checkIntervalMinutes * MILLISECONDS_IN_MINUTE;
            data.alertingData.statusUpdateIntervalMs =
              data.alertingData.statusUpdateIntervalMinutes * MILLISECONDS_IN_MINUTE;
            data.alertingData.activeAlertNotificationIntervalMs =
              data.alertingData.activeAlertNotificationIntervalMinutes * MILLISECONDS_IN_MINUTE;
            data.alertingData = _.omit(
              data.alertingData,
              'checkIntervalMinutes',
              'statusUpdateIntervalMinutes',
              'activeAlertNotificationIntervalMinutes'
            );

            updateCustomerDetails(data);
            this.setState({ statusUpdated: true });
            setSubmitting(false);

            // default form to new values to avoid unwanted validation of smtp fields when they are hidden
            resetForm(values);
          }}
        >
          {({ values, handleChange, handleSubmit, isSubmitting }) => (
            <Form name="EditCustomerProfile" onSubmit={handleSubmit}>
              <Row>
                <Col md={6} sm={12}>
                  <h3>Alerting controls</h3>
                  <Field
                    name="callhomeLevel"
                    component={YBControlledSelectWithLabel}
                    label="Callhome Level"
                    input={{ name: 'callhomeLevel' }}
                    onInputChanged={handleChange}
                    selectVal={values.callhomeLevel}
                    options={callhomeOptions}
                    isReadOnly={isReadOnly}
                  />
                  <Field
                    name="alertingData.checkIntervalMinutes"
                    type="text"
                    component={YBFormInput}
                    label="Health check interval (in minutes)"
                    placeholder="Milliseconds to check universe status"
                    disabled={isReadOnly}
                  />
                  <Field
                    name="alertingData.activeAlertNotificationIntervalMinutes"
                    type="text"
                    component={YBFormInput}
                    label="Active alert notification interval (in minutes)"
                    placeholder="Milliseconds to send an active alert notifications"
                    disabled={isReadOnly}
                  />
                </Col>
              </Row>
              <Row>
                <br />
                <Col md={6} sm={12}>
                  <Field name="emailNotifications">
                    {({ field }) => (
                      <YBToggle
                        onToggle={handleChange}
                        name="emailNotifications"
                        input={{
                          value: field.value,
                          onChange: field.onChange
                        }}
                        label={<h3>Email notifications</h3>}
                        subLabel="Enable health check result email notifications"
                        isReadOnly={isReadOnly}
                      />
                    )}
                  </Field>
                  <div hidden={!values.emailNotifications}>
                    <Field
                      name="alertingData.alertingEmail"
                      type="text"
                      component={YBFormInput}
                      label="Alert emails"
                      placeholder="Emails to forward alerts to"
                      disabled={isReadOnly}
                    />
                    <Field name="alertingData.sendAlertsToYb">
                      {({ field }) => (
                        <YBToggle
                          onToggle={handleChange}
                          name="alertingData.sendAlertsToYb"
                          input={{
                            value: field.value,
                            onChange: field.onChange
                          }}
                          isReadOnly={isReadOnly}
                          label="Send alert emails to YugaByte team"
                          subLabel="Whether or not to send alerting emails to the YugaByte team."
                        />
                      )}
                    </Field>
                    <Field
                      name="smtpData.emailFrom"
                      type="text"
                      component={YBFormInput}
                      label="Email From"
                      placeholder="Send outgoing emails from"
                      disabled={isReadOnly}
                    />
                    <Field
                      name="smtpData.smtpServer"
                      type="text"
                      component={YBFormInput}
                      label="SMTP Server"
                      placeholder="SMTP server address"
                      disabled={isReadOnly}
                    />
                    <Field
                      name="smtpData.smtpPort"
                      type="text"
                      component={YBFormInput}
                      label="SMTP Port"
                      placeholder="SMTP server port"
                      disabled={isReadOnly}
                    />
                    <Field
                      name="smtpData.smtpUsername"
                      type="text"
                      component={YBFormInput}
                      label="SMTP Username"
                      placeholder="SMTP server username"
                      disabled={isReadOnly}
                    />
                    <Field
                      name="smtpData.smtpPassword"
                      type="SMTP password"
                      autoComplete="new-password"
                      component={YBFormInput}
                      label="SMTP Password"
                      placeholder="SMTP server password"
                      disabled={isReadOnly}
                    />
                    <Field name="smtpData.useSSL">
                      {({ field }) => (
                        <YBToggle
                          onToggle={handleChange}
                          name="smtpData.useSSL"
                          input={{
                            value: field.value,
                            onChange: field.onChange
                          }}
                          label="SMTP use SSL"
                          subLabel="Whether or not to use SSL for SMTP server connection."
                          isReadOnly={isReadOnly}
                        />
                      )}
                    </Field>
                    <Field name="smtpData.useTLS">
                      {({ field }) => (
                        <YBToggle
                          onToggle={handleChange}
                          name="smtpData.useTLS"
                          input={{
                            value: field.value,
                            onChange: field.onChange
                          }}
                          label="SMTP use TLS"
                          subLabel="Whether or not to use TLS for SMTP server connection."
                          isReadOnly={isReadOnly}
                        />
                      )}
                    </Field>
                    <Field
                      name="alertingData.statusUpdateIntervalMinutes"
                      type="text"
                      component={YBFormInput}
                      label="Health Check email report interval (in minutes)"
                      placeholder="Milliseconds to send a status report email"
                      disabled={isReadOnly}
                    />
                    <Field name="alertingData.reportOnlyErrors">
                      {({ field }) => (
                        <YBToggle
                          onToggle={handleChange}
                          name="alertingData.reportOnlyErrors"
                          input={{
                            value: field.value,
                            onChange: field.onChange
                          }}
                          label="Only include errors in alert emails"
                          subLabel="Whether or not to include errors in alert emails."
                          isReadOnly={isReadOnly}
                        />
                      )}
                    </Field>
                  </div>
                </Col>
              </Row>
              {!isReadOnly && (
                <div className="form-action-button-container">
                  <Col sm={12}>
                    <RbacValidator
                    accessRequiredOn={{
                      ...UserPermissionMap.editAlertsConfig
                    }}
                    overrideStyle={{
                      float: 'right'
                    }}
                    isControl
                    >
                    <YBButton
                      btnText="Save"
                      btnType="submit"
                      disabled={isSubmitting}
                      btnClass="btn btn-orange pull-right"
                    />
                    </RbacValidator>
                  </Col>
                </div>
              )}
            </Form>
          )}
        </Formik>
      </div>
    );
  }
}
