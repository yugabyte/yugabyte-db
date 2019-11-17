// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Cookies from 'js-cookie';
import { Row, Col, Tab } from 'react-bootstrap';
import { YBFormInput, YBButton, YBToggle, YBControlledSelectWithLabel } from '../common/forms/fields';
import { Formik, Form, Field } from 'formik';
import { browserHistory} from 'react-router';
import { isNonAvailable, isDisabled, showOrRedirect, isNotHidden } from 'utils/LayoutUtils';
import { FlexContainer, FlexGrow, FlexShrink } from '../common/flexbox/YBFlexBox';
import { YBTabsWithLinksPanel } from '../panels';
import { YBCopyButton } from '../common/descriptors';
import _ from 'lodash';
import * as Yup from 'yup';
import { isDefinedNotNull, isNonEmptyObject } from 'utils/ObjectUtils';
import { getPromiseState } from '../../utils/PromiseUtils';

// TODO set predefined defaults another way not to share defaults this way
const CHECK_INTERVAL_MS = 300000;
const STATUS_UPDATE_INTERVAL_MS = 43200000;

export default class CustomerProfile extends Component {
  constructor(props) {
    super(props);
    this.state = {
      statusUpdated: false
    };
  }

  componentWillMount() {
    const { customer } = this.props;
    if (isNonAvailable(customer.features, "main.profile")) browserHistory.push('/');
  }

  handleRefreshApiToken = (e) => {
    const { refreshApiToken } = this.props;
    const authToken = Cookies.get("authToken") || localStorage.getItem('authToken');
    e.stopPropagation();
    e.preventDefault();
    refreshApiToken({"X-AUTH-TOKEN": authToken});
  }

  render() {
    const {
      customer = {},
      apiToken,
      customerProfile,
      updateCustomerDetails,
      params
    } = this.props;

    showOrRedirect(customer.features, "main.profile");

    let profileUpdateStatus = <span/>;
    if (customerProfile.data === "updated-success" && this.state.statusUpdated) {
      profileUpdateStatus = <span className="pull-right request-status yb-success-color yb-dissapear">Profile Updated Successfully</span>;
      setTimeout(() => {
        this.setState({statusUpdated: false});
      }, 2000);
    } else if (customerProfile.error && this.state.statusUpdated) {
      profileUpdateStatus = <span className="pull-right request-status yb-fail-color yb-dissapear">Profile Update Failed</span>;
      setTimeout(() => {
        this.setState({statusUpdated: false});
      }, 2000);
    }

    const validationSchema = Yup.object().shape({
      name: Yup.string()
        .required('Enter name'),
    
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
        .oneOf([Yup.ref('confirmPassword')], "Passwords don't match"),

      confirmPassword: Yup.string()
        .notRequired()
        .oneOf([Yup.ref('password')], "Passwords don't match"),

      alertingData: Yup.object({
        sendAlertsToYb: Yup.boolean(),

        alertingEmail: Yup.string().nullable(),

        checkIntervalMs: Yup.number()
          .typeError('Must specify a number')
          .integer(),

        statusUpdateIntervalMs: Yup.number()
          .typeError('Must specify a number')
          .integer(),
      }),
      callhomeLevel: Yup.string()
    });

    const alertingDataProps = ['alertingEmail', 'checkIntervalMs', 'statusUpdateIntervalMs', 'sendAlertsToYb'];
    
    const initialValues = {
      name: customer.name || '',
      email: customer.email || '',
      code: customer.code || '',
      alertingData: {
        // alertingData properties
        alertingEmail: customer.alertingData ?
          customer.alertingData.alertingEmail || '':
          '',
        checkIntervalMs: isNonEmptyObject(customer.alertingData) ?
          customer.alertingData.checkIntervalMs :
          CHECK_INTERVAL_MS,
        statusUpdateIntervalMs: isNonEmptyObject(customer.alertingData) ?
          customer.alertingData.statusUpdateIntervalMs :
          STATUS_UPDATE_INTERVAL_MS,
        sendAlertsToYb: customer.alertingData && customer.alertingData.sendAlertsToYb,
      },
      
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

    const defaultTab = isNotHidden(customer.features, "main.profile") ? "general" : "general";
    const activeTab = isDefinedNotNull(params) ? params.tab : defaultTab;

    return (
      <div className="bottom-bar-padding">
        <h2 className="content-title">Update Customer Profile {profileUpdateStatus}</h2>
        <Formik
          validationSchema={validationSchema}
          initialValues={initialValues}
          enableReinitialize
          onSubmit={(values, { setSubmitting }) => {
            /**
             * Generate object to be sent with only changes from the `initialValues` object.
             * Properties in `alertingDataProps` will be moved under `alertingData` key.
             */
            this.setState({statusUpdated: true});
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
              <YBTabsWithLinksPanel defaultTab={defaultTab} activeTab={activeTab} routePrefix={`/profile/`} id={"profile-tab-panel"} className="profile-detail">
                {[
                  <Tab.Pane
                    eventKey={"general"}
                    title="General"
                    key="general-tab"
                    mountOnEnter={true}
                    unmountOnExit={true}
                    disabled={isDisabled(customer.features, "main.profile")}>
                    <Row>
                      <Col md={6} sm={12}>
                        <Row>
                          <Col sm={12}>
                            <h3>Profile Info</h3>
                            <Field name="name" type="text" component={YBFormInput} placeholder="Full Name" label="Full Name"/>
                            <Field name="email" readOnly={false} type="text" label="Email" component={YBFormInput} placeholder="Email Address" />
                            <Field name="code" readOnly={true} type="text" label="Environment" component={YBFormInput} placeholder="Customer Code" />
                          </Col>
                        </Row>
                        <Row>
                          <Col sm={12}>
                            <br/>
                            <h3>Change Password</h3>
                            <Field name="password" type="password" component={YBFormInput} label="Password" autoComplete="new-password" placeholder="Enter New Password"/>
                            <Field name="confirmPassword" type="password" component={YBFormInput} label="Confirm Password" autoComplete="new-password" placeholder="Confirm New Password"/>
                          </Col>
                        </Row>
                      </Col>
                      <Col md={6} sm={12}>
                        <h3>API Key management</h3>
                        <FlexContainer>
                          <FlexGrow className="api-token-component">
                            <Field
                              field={{value: apiToken.data || customer.apiToken || ""}}
                              type="text"
                              readOnly={true}
                              component={YBFormInput}
                              label="API Token"
                              placeholder="Press Generate Key"
                            />
                            <YBCopyButton text={apiToken.data || customer.apiToken || ""}/>
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
                  </Tab.Pane>,

                  <Tab.Pane
                    eventKey={"health-alerting"}
                    title="Health & Alerting"
                    key="health-alerting-tab"
                    mountOnEnter={true}
                    unmountOnExit={true}
                    disabled={isDisabled(customer.features, "main.profile")}>
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
                        <Field name="alertingData.checkIntervalMs" type="text" component={YBFormInput} label="Health check interval" placeholder="Miliseconds to check universe status"/>
                        <Field name="alertingData.statusUpdateIntervalMs" type="text" component={YBFormInput} label="Report email interval" placeholder="Miliseconds to send a status report email"/>
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
                  </Tab.Pane>
                ]}
              </YBTabsWithLinksPanel>
            </Form>
          )}
        />
      </div>
    );
  }
}
