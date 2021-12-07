// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Cookies from 'js-cookie';
import { isEqual } from 'lodash';
import { Col, Row } from 'react-bootstrap';
import { YBButton, YBFormInput, YBFormSelect } from '../common/forms/fields';
import { Field, Form, Formik } from 'formik';
import { showOrRedirect } from '../../utils/LayoutUtils';
import { FlexContainer, FlexGrow, FlexShrink } from '../common/flexbox/YBFlexBox';
import { YBCopyButton } from '../common/descriptors';
import * as Yup from 'yup';
import { isNonEmptyArray } from '../../utils/ObjectUtils';
import { getPromiseState } from '../../utils/PromiseUtils';

import moment from 'moment';

const MIN_PASSWORD_LENGTH = 8;

export default class UserProfileForm extends Component {
  constructor(props) {
    super(props);
    this.state = {
      statusUpdated: false
    };
  }

  componentDidMount() {
    const { validateRegistration } = this.props;
    validateRegistration();
  }

  handleRefreshApiToken = (e) => {
    const { refreshApiToken } = this.props;
    const authToken = Cookies.get('authToken') || localStorage.getItem('authToken');
    e.stopPropagation();
    e.preventDefault();
    refreshApiToken({ 'X-AUTH-TOKEN': authToken });
  };

  UNSAFE_componentWillReceiveProps(nextProps) {
    const { customerProfile, handleProfileUpdate } = this.props;
    const hasProfileChanged =
      getPromiseState(customerProfile) !== getPromiseState(nextProps.customerProfile) &&
      (getPromiseState(nextProps.customerProfile).isSuccess() ||
        getPromiseState(nextProps.customerProfile).isError());
    if (this.state.statusUpdated && hasProfileChanged) {
      handleProfileUpdate(nextProps.customerProfile.data);
      this.setState({ statusUpdated: false });
    }
  }

  formatTimezoneLabel = (timezone) => {
    const formattedTimezone = timezone.replace('_', ' ');
    return formattedTimezone + ' UTC' + moment.tz(timezone).format('ZZ');
  };

  render() {
    const {
      customer = {},
      users = [],
      apiToken,
      updateCustomerDetails,
      updateUserProfile,
      passwordValidationInfo,
      currentUser
    } = this.props;
    const minPasswordLength = passwordValidationInfo?.minLength || MIN_PASSWORD_LENGTH;

    showOrRedirect(customer.data.features, 'main.profile');

    const validationSchema = Yup.object().shape({
      name: Yup.string().required('Enter name'),

      // Regex below matches either the default value 'admin' or a generic email address
      email: Yup.string()
        .matches(
          /(^admin$)|(^[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}$)/i,
          'This is not a valid email or value'
        )
        .required('Enter email'),

      code: Yup.string()
        .required('Enter Environment name')
        .max(5, 'Environment name can be only 5 characters long'),

      password: Yup.string()
        .notRequired()
        .min(
          minPasswordLength,
          `Password is too short - must be ${minPasswordLength} characters minimum.`
        )
        .matches(
          /^(?=.*[0-9])(?=.*[!@#$%^&*])(?=.*[a-z])(?=.*[A-Z])[a-zA-Z0-9!@#$%^&*]{8,256}$/,
          `Password must contain at least ${passwordValidationInfo?.minDigits} digit
          , ${passwordValidationInfo?.minUppercase} capital
          , ${passwordValidationInfo?.minLowercase} lowercase
          and ${passwordValidationInfo?.minSpecialCharacters} of the !@#$%^&* (special) characters.`
        )
        .oneOf([Yup.ref('confirmPassword')], "Passwords don't match"),

      confirmPassword: Yup.string()
        .notRequired()
        .oneOf([Yup.ref('password')], "Passwords don't match")
    });

    // Filter users for userUUID set during login
    const loginUserId = localStorage.getItem('userId');
    const getCurrentUser = isNonEmptyArray(users)
      ? users.filter((u) => u.uuid === loginUserId)
      : [];

    const defaultTimezoneOption = { value: '', label: 'Default' };
    const initialValues = {
      name: customer.data.name || '',
      email: (getCurrentUser.length && getCurrentUser[0].email) || '',
      code: customer.data.code || '',
      customerId: customer.data.uuid,
      password: '',
      confirmPassword: '',
      timezone: currentUser.data.timezone
        ? {
            value: currentUser.data.timezone,
            label: this.formatTimezoneLabel(currentUser.data.timezone)
          }
        : defaultTimezoneOption
    };
    const timezoneOptions = [defaultTimezoneOption];
    moment.tz.names().forEach((timezone) => {
      timezoneOptions.push({
        value: timezone,
        label: this.formatTimezoneLabel(timezone)
      });
    });

    return (
      <div className="bottom-bar-padding">
        <Formik
          validationSchema={validationSchema}
          initialValues={initialValues}
          enableReinitialize
          onSubmit={(values, { setSubmitting }) => {
            const payload = {
              ...values,
              timezone: values.timezone.value
            };
            const initialPayload = {
              ...initialValues,
              timezone: initialValues.timezone.value
            };
            // Compare values to initial values to see if changes were made
            let hasNameChanged = false;
            let hasUserProfileChanged = false;
            Object.entries(payload).forEach(([key, value]) => {
              if (!isEqual(value, initialPayload[key])) {
                if (key === 'name') {
                  hasNameChanged = true;
                } else {
                  hasUserProfileChanged = true;
                }
              }
            });
            if (hasNameChanged) {
              updateCustomerDetails(payload);
            }
            if (hasUserProfileChanged) {
              updateUserProfile(getCurrentUser[0], payload);
            }
            setSubmitting(false);
            this.setState({ statusUpdated: hasNameChanged || hasUserProfileChanged });
          }}
        >
          {({ handleSubmit, isSubmitting }) => (
            <Form name="EditCustomerProfile" onSubmit={handleSubmit}>
              <Row>
                <Col md={6} sm={12}>
                  <Row>
                    <Col sm={12}>
                      <h3>Profile Info</h3>
                      <Field
                        name="name"
                        type="text"
                        component={YBFormInput}
                        placeholder="Full Name"
                        label="Full Name"
                      />
                      <Field
                        name="email"
                        readOnly={true}
                        type="text"
                        label="Email"
                        component={YBFormInput}
                        placeholder="Email Address"
                      />
                      <Field
                        name="code"
                        readOnly={true}
                        type="text"
                        label="Environment"
                        component={YBFormInput}
                        placeholder="Customer Code"
                      />
                      <Field
                        name="timezone"
                        label="Preferred Timezone"
                        component={YBFormSelect}
                        options={timezoneOptions}
                        placeholder="User Timezone"
                      />
                    </Col>
                  </Row>
                  <Row>
                    <Col sm={12}>
                      <br />
                      <h3>Change Password</h3>
                      <Field
                        name="password"
                        type="password"
                        component={YBFormInput}
                        label="Password"
                        autoComplete="new-password"
                        placeholder="Enter New Password"
                      />
                      <Field
                        name="confirmPassword"
                        type="password"
                        component={YBFormInput}
                        label="Confirm Password"
                        autoComplete="new-password"
                        placeholder="Confirm New Password"
                      />
                    </Col>
                  </Row>
                </Col>
                <Col md={6} sm={12}>
                  <h3>API Key management</h3>
                  <FlexContainer>
                    <FlexGrow className="api-token-component">
                      <Field
                        field={{ value: apiToken.data || customer.data.apiToken || '' }}
                        type="text"
                        readOnly={true}
                        component={YBFormInput}
                        label="API Token"
                        placeholder="Press Generate Key"
                      />
                      <YBCopyButton text={apiToken.data || customer.data.apiToken || ''} />
                    </FlexGrow>
                    <FlexShrink>
                      <YBButton
                        btnText="Generate Key"
                        btnType="button"
                        loading={getPromiseState(apiToken).isLoading()}
                        onClick={this.handleRefreshApiToken}
                        btnClass="btn btn-orange pull-right btn-api-token"
                      />
                    </FlexShrink>
                  </FlexContainer>
                  <Field
                    name="customerId"
                    readOnly={true}
                    type="text"
                    label="Customer ID"
                    component={YBFormInput}
                    placeholder="Customer ID"
                  />
                </Col>
              </Row>
              <div className="form-action-button-container">
                <Col sm={12}>
                  <YBButton
                    btnText="Save"
                    btnType="submit"
                    disabled={isSubmitting}
                    btnClass="btn btn-orange pull-right"
                  />
                </Col>
              </div>
            </Form>
          )}
        </Formik>
      </div>
    );
  }
}
