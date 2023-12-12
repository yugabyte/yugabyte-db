// Copyright (c) YugaByte, Inc.
import { Component } from 'react';
import * as Yup from 'yup';
import Cookies from 'js-cookie';
import moment from 'moment-timezone';
import { isEqual } from 'lodash';
import { Col, Row } from 'react-bootstrap';
import { FormHelperText } from '@material-ui/core';
import { YBButton, YBFormInput, YBFormSelect, YBTextArea } from '../common/forms/fields';
import { Field, Form, Formik } from 'formik';
import { showOrRedirect, isNotHidden, isDisabled, isHidden } from '../../utils/LayoutUtils';
import { FlexContainer, FlexGrow, FlexShrink } from '../common/flexbox/YBFlexBox';
import { YBCopyButton } from '../common/descriptors';
import { isDefinedNotNull, isNonEmptyString } from '../../utils/ObjectUtils';
import { getPromiseState } from '../../utils/PromiseUtils';
import { UserTypes } from '../../redesign/features/rbac/users/interface/Users';

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

  getOIDCToken = () => {
    const { currentUser, fetchOIDCToken } = this.props;
    fetchOIDCToken(currentUser.data.uuid);
  };

  handleRefreshApiToken = (e) => {
    const { refreshApiToken } = this.props;
    const authToken = Cookies.get('authToken') ?? localStorage.getItem('authToken');
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
      apiToken,
      OIDCToken,
      isOIDCEnhancementEnabled,
      updateCustomerDetails,
      updateUserProfile,
      updateUserPassword,
      passwordValidationInfo,
      currentUser
    } = this.props;
    const minPasswordLength = passwordValidationInfo?.minLength || MIN_PASSWORD_LENGTH;
    showOrRedirect(customer.data.features, 'main.profile');

    const userRole = currentUser?.data?.role;
    const isSuperAdmin = ['SuperAdmin'].includes(userRole);
    const isLDAPUser = !!currentUser?.data?.userType === UserTypes.LDAP;
    const defaultTimezoneOption = { value: '', label: 'Default' };
    const initialValues = {
      name: customer.data.name || '',
      email: currentUser.data.email || '',
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

    const validationSchema = Yup.object().shape({
      name: Yup.string().required('Enter name'),

      // Regex below matches either the default value 'admin' or a generic email address
      email: isLDAPUser
        ? Yup.string().required('Enter Email or Username')
        : Yup.string()
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

    const waringMessageContent = (
      <FlexShrink className="token-warning">
        <FormHelperText className="warning-color">
          <i className="fa fa-warning" />
          <span>
            {<b>{'Note! '}</b>}
            {'Save the token in a safe place as it’s only temporarily visible.'}
          </span>
        </FormHelperText>
      </FlexShrink>
    );

    return (
      <div className="bottom-bar-padding">
        <Formik
          validationSchema={validationSchema}
          initialValues={initialValues}
          enableReinitialize
          onSubmit={(values, { setSubmitting }) => {
            const payload = {
              ...values
            };
            const initialPayload = {
              ...initialValues,
              timezone: initialValues.timezone.value
            };

            if (
              isDisabled(customer.data.features, 'profile.profileInfo') ||
              isHidden(customer.data.features, 'profile.profileInfo')
            ) {
              if (isDefinedNotNull(currentUser.data.timezone))
                payload.timezone = currentUser.data.timezone;
              else delete payload.timezone;
            } else {
              payload.timezone = values.timezone.value;
            }

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
              if (
                isDisabled(customer.data.features, 'profile.profileInfo') ||
                isHidden(customer.data.features, 'profile.profileInfo')
              )
                updateUserPassword(currentUser.data, payload);
              else updateUserProfile(currentUser.data, payload);
            }
            setSubmitting(false);
            this.setState({ statusUpdated: hasNameChanged || hasUserProfileChanged });
          }}
        >
          {({ handleSubmit, isSubmitting }) => (
            <Form name="EditCustomerProfile" onSubmit={handleSubmit}>
              <Row>
                <Col md={6} sm={12}>
                  {isNotHidden(customer.data.features, 'profile.profileInfo') && (
                    <Row>
                      <Col sm={12}>
                        <h3>Profile Info</h3>
                        <Field
                          name="name"
                          readOnly={isDisabled(customer.data.features, 'profile.profileInfo')}
                          type="text"
                          component={YBFormInput}
                          placeholder="Customer Name"
                          label="Customer Name"
                        />
                        <Field
                          name="email"
                          readOnly={true}
                          type="text"
                          label="Email or Username"
                          component={YBFormInput}
                          placeholder="Email or Username"
                        />
                        <Field
                          name="code"
                          readOnly={true}
                          type="text"
                          label="Environment"
                          component={YBFormInput}
                          placeholder="Customer Code"
                        />
                        <span className="copy-text-field">
                          <Field
                            name="customerId"
                            readOnly={true}
                            type="text"
                            label="Customer ID"
                            component={YBFormInput}
                            placeholder="Customer ID"
                          />
                          <YBCopyButton text={customer.data.uuid} />
                        </span>
                        <Field
                          name="timezone"
                          isDisabled={isDisabled(customer.data.features, 'profile.profileInfo')}
                          label="Preferred Timezone"
                          component={YBFormSelect}
                          options={timezoneOptions}
                          placeholder="User Timezone"
                        />
                      </Col>
                    </Row>
                  )}
                  {!isLDAPUser && (
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
                  )}
                </Col>
                {isNotHidden(customer.data.features, 'profile.apiKeyManagement') && (
                  <Col md={6} sm={12}>
                    <h3>Key management</h3>
                    <FlexContainer>
                      <FlexGrow className="copy-text-field">
                        <Field
                          field={{ value: apiToken.data || customer.data.apiToken || '' }}
                          type="text"
                          readOnly={true}
                          component={YBFormInput}
                          label="API Token"
                          placeholder="Press Generate Key"
                        />
                        <YBCopyButton
                          text={apiToken.data || customer.data.apiToken || ''}
                          disabled={isDisabled(customer.data.features, 'profile.apiKeyManagement')}
                        />
                      </FlexGrow>
                      <FlexShrink>
                        <YBButton
                          btnText="Generate Key"
                          btnType="button"
                          loading={getPromiseState(apiToken).isLoading()}
                          onClick={this.handleRefreshApiToken}
                          btnClass="btn btn-orange pull-right btn-api-token"
                          disabled={isDisabled(customer.data.features, 'profile.apiKeyManagement')}
                        />
                      </FlexShrink>
                    </FlexContainer>
                    {isNonEmptyString(apiToken.data || customer.data.apiToken) &&
                      waringMessageContent}
                    {!isSuperAdmin && isOIDCEnhancementEnabled && (
                      <>
                        <FlexContainer>
                          <FlexGrow className="copy-text-field">
                            <YBTextArea
                              type="text"
                              rows={OIDCToken?.data?.oidcAuthToken ? 5 : 2}
                              isReadOnly={true}
                              input={{
                                value: OIDCToken?.data?.oidcAuthToken
                              }}
                              label="OIDC Token"
                            />
                            <YBCopyButton text={OIDCToken?.data?.oidcAuthToken || ''} />
                          </FlexGrow>
                          <FlexShrink>
                            <YBButton
                              btnText="Fetch OIDC Token"
                              btnType="button"
                              loading={getPromiseState(OIDCToken).isLoading()}
                              onClick={this.getOIDCToken}
                              btnClass="btn btn-orange pull-right btn-api-token"
                            />
                          </FlexShrink>
                        </FlexContainer>
                        {isNonEmptyString(OIDCToken?.data?.oidcAuthToken) && waringMessageContent}
                      </>
                    )}
                  </Col>
                )}
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
