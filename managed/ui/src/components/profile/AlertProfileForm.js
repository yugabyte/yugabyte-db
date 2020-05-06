// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBFormInput, YBButton, YBToggle,
         YBControlledSelectWithLabel } from '../common/forms/fields';
import { Formik, Form, Field } from 'formik';
import { browserHistory} from 'react-router';
import { isNonAvailable, showOrRedirect } from 'utils/LayoutUtils';
import * as Yup from 'yup';
import { isNonEmptyObject, isNonEmptyArray } from 'utils/ObjectUtils';
import { getPromiseState } from '../../utils/PromiseUtils';

// TODO set predefined defaults another way not to share defaults this way
const CHECK_INTERVAL_MS = 300000;
const STATUS_UPDATE_INTERVAL_MS = 43200000;

export default class AlertProfileForm extends Component {
  constructor(props) {
    super(props);
    this.state = {
      statusUpdated: false
    };
  }

  componentDidMount() {
    const { customer } = this.props;
    this.props.getCustomerUsers();
    if (isNonAvailable(customer.features, "main.profile")) browserHistory.push('/');
  }

  componentDidUpdate() {
    const { customerProfile, handleProfileUpdate } = this.props;
    const { statusUpdated } = this.state;
    if (statusUpdated && (getPromiseState(customerProfile).isSuccess() ||
        getPromiseState(customerProfile).isError())) {
      handleProfileUpdate(customerProfile.data);
      this.setState({statusUpdated: false});
    }
  }

  render() {
    const {
      customer = {},
      users = [],
      updateCustomerDetails
    } = this.props;

    showOrRedirect(customer.data.features, "main.profile");

    const validationSchema = Yup.object().shape({
      alertingData: Yup.object({
        sendAlertsToYb: Yup.boolean()
          .default(false)
          .nullable(),

        alertingEmail: Yup.string().email('Must be an email').nullable(),

        checkIntervalMs: Yup.number()
          .typeError('Must specify a number'),

        statusUpdateIntervalMs: Yup.number()
          .typeError('Must specify a number'),

        reportOnlyErrors: Yup.boolean()
          .default(false)
          .nullable(),
      }),
      callhomeLevel: Yup.string()
    });

    // Filter users for userUUID set during login
    const loginUserId = localStorage.getItem('userId');
    const getCurrentUser = isNonEmptyArray(users) ? users.filter(u => u.uuid === loginUserId) : [];
    const initialValues = {
      name: customer.data.name || '',
      email: (getCurrentUser.length && getCurrentUser[0].email) || '',
      code: customer.data.code || '',

      alertingData: {
        alertingEmail: customer.data.alertingData ?
          customer.data.alertingData.alertingEmail || '' :
          '',
        checkIntervalMs: getPromiseState(customer).isSuccess() ? (
          isNonEmptyObject(customer.data.alertingData) ?
          customer.data.alertingData.checkIntervalMs :
          CHECK_INTERVAL_MS) : '',
        statusUpdateIntervalMs: getPromiseState(customer).isSuccess() ? (
          isNonEmptyObject(customer.data.alertingData) ?
          customer.data.alertingData.statusUpdateIntervalMs :
          STATUS_UPDATE_INTERVAL_MS) : '',
        sendAlertsToYb: customer.data.alertingData && customer.data.alertingData.sendAlertsToYb,
        reportOnlyErrors: customer.data.alertingData && customer.data.alertingData.reportOnlyErrors,
      },
      callhomeLevel: customer.data.callhomeLevel || 'NONE'
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
            updateCustomerDetails(values);
            this.setState({statusUpdated: true});
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
                <Col md={6} sm={12}>
                  <h3>Alerting controls</h3>
                  <Field
                    name="alertingData.alertingEmail"
                    type="text"
                    component={YBFormInput}
                    label="Alert email"
                    placeholder="Email to forward alerts to"
                  />
                  <Field name="alertingData.sendAlertsToYb">
                    {({field, form}) => (<YBToggle onToggle={handleChange}
                        name="alertingData.sendAlertsToYb"
                        input={{
                          value: field.value,
                          onChange: field.onChange,
                        }}
                        label="Send alert emails to YugaByte team"
                        subLabel="Whether or not to send alerting emails to the YugaByte team."
                      />
                    )}
                  </Field>
                  <Field name="callhomeLevel"
                    component={YBControlledSelectWithLabel}
                    label="Callhome Level"
                    input={{name: 'callhomeLevel'}}
                    onInputChanged={handleChange}
                    selectVal={values.callhomeLevel}
                    options={callhomeOptions}
                  />
                  <Field name="alertingData.checkIntervalMs" type="text"
                         component={YBFormInput} label="Health check interval"
                         placeholder="Miliseconds to check universe status"/>
                  <Field name="alertingData.statusUpdateIntervalMs" type="text"
                         component={YBFormInput} label="Report email interval"
                         placeholder="Miliseconds to send a status report email"/>
                  <Field name="alertingData.reportOnlyErrors">
                    {({field, form}) => (<YBToggle onToggle={handleChange}
                        name="alertingData.reportOnlyErrors"
                        input={{
                          value: field.value,
                          onChange: field.onChange,
                        }}
                        label="Only include errors in alert emails"
                        subLabel="Whether or not to include errors in alert emails."
                      />
                    )}
                  </Field>
                </Col>
              </Row>
              <div className="form-action-button-container">
                <Col sm={12}>
                  <YBButton btnText="Save"
                    btnType="submit"
                    disabled={isSubmitting}
                    btnClass="btn btn-orange pull-right"
                  />
                </Col>
              </div>
            </Form>
          )}
        />
      </div>
    );
  }
}
