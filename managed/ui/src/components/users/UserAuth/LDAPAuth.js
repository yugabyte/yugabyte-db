// Copyright (c) YugaByte, Inc.

import React, { useEffect, useState } from 'react';
import * as Yup from 'yup';
import { trimStart, trimEnd } from 'lodash';
import { toast } from 'react-toastify';
import { Row, Col, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { Formik, Form, Field } from 'formik';
import { YBFormInput, YBButton, YBModal, YBToggle } from '../../common/forms/fields';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { YUGABYTE_TITLE } from '../../../config';
import WarningIcon from '../icons/warning_icon';
import Bulb from '../../universes/images/bulb.svg';

const VALIDATION_SCHEMA = Yup.object().shape({
  ldap_url: Yup.string()
    .matches(/^(?:(http|https|ldap|ldaps)?:\/\/)?[\w.-]+(?:[\w-]+)+:\d{1,5}$/, {
      message: 'LDAP URL must be a valid URL with port number'
    })
    .required('LDAP URL is Required'),
  ldap_security: Yup.string().required('Please select connection security'),
  ldap_service_account_password: Yup.string().when('ldap_service_account_username', {
    is: (username) => username && username.length > 0,
    then: Yup.string().required('Password is Required'),
    otherwise: Yup.string()
  }),
  ldap_search_attribute: Yup.string().when('use_search_and_bind', {
    is: (use_search_and_bind) => use_search_and_bind === 'true',
    then: Yup.string().required('Search Attribute is Required'),
    otherwise: Yup.string()
  })
});

const LDAP_PATH = 'yb.security.ldap';

const TOAST_OPTIONS = { autoClose: 1750 };

const SECURITY_OPTIONS = [
  {
    label: 'Secure LDAP (LDAPS)',
    value: 'enable_ldaps'
  },
  {
    label: 'LDAP with StartTLS',
    value: 'enable_ldap_start_tls'
  },
  {
    label: 'None',
    value: 'unsecure'
  }
];

const AUTH_MODES = [
  {
    label: 'Simple Bind',
    value: false
  },
  {
    label: 'Search and Bind',
    value: true
  }
];

export const LDAPAuth = (props) => {
  const {
    fetchRunTimeConfigs,
    setRunTimeConfig,
    deleteRunTimeConfig,
    runtimeConfigs: {
      data: { configEntries }
    },
    isTabsVisible
  } = props;
  const [showToggle, setToggleVisible] = useState(false);
  const [dialog, showDialog] = useState(false);
  const [ldapEnabled, setLDAP] = useState(false);

  const transformData = (values) => {
    const splittedURL = values.ldap_url.split(':');
    const ldap_port = splittedURL[splittedURL.length - 1];
    const ldap_url = values.ldap_url.replace(`:${ldap_port}`, '');
    const security = values.ldap_security;
    const use_search_and_bind = values.use_search_and_bind;

    const transformedData = {
      ...values,
      ldap_url: ldap_url ?? '',
      ldap_port: ldap_port ?? '',
      enable_ldaps: `${security === 'enable_ldaps'}`,
      enable_ldap_start_tls: `${security === 'enable_ldap_start_tls'}`
    };

    if (use_search_and_bind === 'false') {
      transformedData.ldap_search_attribute = '';
    }

    delete transformedData.ldap_security;

    return transformedData;
  };

  const escapeStr = (str) => {
    let s = trimStart(str, '""');
    s = trimEnd(s, '""');
    return s;
  };

  const initializeFormValues = () => {
    const ldapConfigs = configEntries.filter((config) => config.key.includes(LDAP_PATH));
    const formData = ldapConfigs.reduce((fData, config) => {
      const [, key] = config.key.split(`${LDAP_PATH}.`);
      fData[key] = escapeStr(config.value);
      return fData;
    }, {});

    let finalFormData = {
      ...formData,
      use_search_and_bind: formData.use_search_and_bind ?? false,
      ldap_url: formData.ldap_url ? [formData.ldap_url, formData.ldap_port].join(':') : ''
    };

    //transform security data
    const { enable_ldaps, enable_ldap_start_tls } = formData;
    if (showToggle) {
      const ldap_security =
        enable_ldaps === 'true'
          ? 'enable_ldaps'
          : enable_ldap_start_tls === 'true'
          ? 'enable_ldap_start_tls'
          : 'unsecure';
      finalFormData = { ...finalFormData, ldap_security };
    }

    return finalFormData;
  };

  const saveLDAPConfigs = async (values) => {
    const formValues = transformData(values);
    const initValues = initializeFormValues();
    const promiseArray = Object.keys(formValues).reduce((promiseArr, key) => {
      if (formValues[key] !== initValues[key]) {
        promiseArr.push(
          formValues[key] !== ''
            ? setRunTimeConfig({
                key: `${LDAP_PATH}.${key}`,
                value: formValues[key]
              })
            : deleteRunTimeConfig({
                key: `${LDAP_PATH}.${key}`
              })
        );
      }

      return promiseArr;
    }, []);

    //enable ldap on save
    promiseArray.push(
      setRunTimeConfig({
        key: `${LDAP_PATH}.use_ldap `,
        value: true
      })
    );

    try {
      toast.warn('Please wait. LDAP configuration is getting saved', TOAST_OPTIONS);
      const response = await Promise.all(promiseArray);
      response && fetchRunTimeConfigs();
      toast.success('LDAP configuration is saved successfully', TOAST_OPTIONS);
    } catch {
      toast.error('Failed to save LDAP configuration', TOAST_OPTIONS);
    }
  };

  const handleToggle = async (e) => {
    const value = e.target.checked;

    if (!value) showDialog(true);
    else {
      setLDAP(true);
      await setRunTimeConfig({
        key: `${LDAP_PATH}.use_ldap`,
        value: true
      });
      toast.success(`LDAP authentication is enabled`, TOAST_OPTIONS);
    }
  };

  const handleDialogClose = () => {
    showDialog(false);
  };

  const handleDialogSubmit = async () => {
    setLDAP(false);
    showDialog(false);
    await setRunTimeConfig({
      key: `${LDAP_PATH}.use_ldap`,
      value: 'false'
    });
    toast.warn(`LDAP authentication is disabled`, TOAST_OPTIONS);
  };

  useEffect(() => {
    const ldapConfig = configEntries.find((config) => config.key.includes(`${LDAP_PATH}.use_ldap`));
    setToggleVisible(!!ldapConfig);
    setLDAP(escapeStr(ldapConfig?.value) === 'true');
  }, [configEntries, setToggleVisible, setLDAP]);

  return (
    <div className="bottom-bar-padding">
      <YBModal
        title="Disable LDAP"
        visible={dialog}
        showCancelButton={true}
        submitLabel="Disable LDAP"
        cancelLabel="Cancel"
        cancelBtnProps={{
          className: 'btn btn-default pull-left ldap-cancel-btn'
        }}
        onHide={handleDialogClose}
        onFormSubmit={handleDialogSubmit}
      >
        <div className="ldap-modal-c">
          <div className="ldap-modal-c-icon">
            <WarningIcon />
          </div>
          <div className="ldap-modal-c-content">
            <b>Note!</b> Users authenticated via LDAP will no longer be able to login if you disable
            LDAP. Are you sure?
          </div>
        </div>
      </YBModal>
      <Col>
        <Formik
          validationSchema={VALIDATION_SCHEMA}
          initialValues={initializeFormValues()}
          enableReinitialize
          onSubmit={(values, { setSubmitting, resetForm }) => {
            saveLDAPConfigs(values);
            setSubmitting(false);
            resetForm(values);
          }}
        >
          {({ handleSubmit, isSubmitting, errors, dirty, values }) => {
            const isDisabled = !ldapEnabled && showToggle;
            const isSaveDisabled = !dirty;

            const LDAPToggle = () => (
              <YBToggle
                onToggle={handleToggle}
                name="use_ldap"
                input={{
                  value: ldapEnabled,
                  onChange: () => {}
                }}
                isReadOnly={!showToggle}
              />
            );

            const LDAPToggleTooltip = () => (
              <OverlayTrigger
                placement="top"
                overlay={
                  <Tooltip className="high-index" id="ldap-toggle-tooltip">
                    To enable LDAP you need to provide and save the required configurations
                  </Tooltip>
                }
              >
                <div>
                  <LDAPToggle />
                </div>
              </OverlayTrigger>
            );

            return (
              <Form name="LDAPConfigForm" onSubmit={handleSubmit}>
                <Row className="ua-field-row">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c ua-title-c">
                        {!isTabsVisible && <h5>LDAP Configuration</h5>}
                      </Col>

                      <Col className="ua-toggle-c">
                        <>
                          <Col className="ua-toggle-label-c">
                            LDAP Enabled &nbsp;
                            <YBInfoTip
                              title="LDAP Enabled"
                              content="Enable or Disable LDAP Authentication"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </Col>

                          {showToggle ? <LDAPToggle /> : <LDAPToggleTooltip />}
                        </>
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="ldap_url">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          LDAP URL &nbsp;
                          <YBInfoTip
                            title="LDAP URL"
                            content="LDAP URL must be a valid URL with port number, Ex:- 0.0.0.0:0000"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="ldap_url"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="connection_security">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Security Protocol &nbsp;
                          <YBInfoTip
                            customClass="ldap-info-popover"
                            title="Security Protocol"
                            content="Configure the LDAP server connection to use LDAPS (SSL) or LDAP with StartTLS (TLS) encryption"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field ua-radio-c">
                        <Row className="ua-radio-field-c">
                          {SECURITY_OPTIONS.map(({ label, value }) => (
                            <Col key={`security-${value}`} className="ua-radio-field">
                              <Field
                                name={'ldap_security'}
                                type="radio"
                                component="input"
                                value={value}
                                checked={`${value}` === `${values['ldap_security']}`}
                                disabled={isDisabled}
                              />
                              &nbsp;&nbsp;{label}
                            </Col>
                          ))}
                        </Row>
                        <Row className="has-error">
                          {errors.ldap_security && (
                            <div className="help-block standard-error">
                              <span>{errors.ldap_security}</span>
                            </div>
                          )}
                        </Row>
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="ldap_basedn">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          LDAP Base DN <br />
                          (Optional) &nbsp;
                          <YBInfoTip
                            customClass="ldap-info-popover"
                            title="LDAP Base DN"
                            content="Base of the DN for LDAP queries. Will be appended to the user name at login time to query the LDAP server"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="ldap_basedn"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="ldap_dn_prefix">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          LDAP DN Prefix <br />
                          (Optional)&nbsp;
                          <YBInfoTip
                            title="LDAP DN Prefix"
                            content="LDAP DN Prefix will be set to CN= by default if not provided"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="ldap_dn_prefix"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="ldap_customeruuid">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          LDAP Customer UUID <br />
                          (Optional) &nbsp;
                          <YBInfoTip
                            title="LDAP Customer UUID"
                            content="Unique ID of the customer in case of multi-tenant platform"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="ldap_customeruuid"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="auth_mode">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Binding Mechanism &nbsp;
                          <YBInfoTip
                            customClass="ldap-info-popover"
                            title="Binding Mechanism"
                            content="Mechanism used to bind to the LDAP server"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field ua-radio-c">
                        <Row className="ua-radio-field-c">
                          {AUTH_MODES.map(({ label, value }) => (
                            <Col key={`auth-mode-${value}`} className="ua-auth-radio-field">
                              <Field
                                name={'use_search_and_bind'}
                                type="radio"
                                component="input"
                                value={value}
                                checked={`${value}` === `${values['use_search_and_bind']}`}
                                disabled={isDisabled}
                              />
                              &nbsp;&nbsp;{label}
                            </Col>
                          ))}
                        </Row>
                      </Col>
                    </Row>
                  </Col>
                </Row>

                {values?.use_search_and_bind === 'true' && (
                  <Row key="ldap_search_attribute">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <Row className="ua-field-row">
                        <Col className="ua-label-c">
                          <div>
                            Search Attribute &nbsp;
                            <YBInfoTip
                              title="Search Attribute"
                              content="Attribute that will be used to search for the user in the LDAP server"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </div>
                        </Col>
                        <Col lg={12} className="ua-field">
                          <Field
                            name="ldap_search_attribute"
                            component={YBFormInput}
                            disabled={isDisabled}
                            className="ua-form-field"
                          />
                        </Col>
                      </Row>
                    </Col>
                  </Row>
                )}

                <br />
                <br />
                <Row className="ua-field-row">
                  <Col lg={9} className=" ua-title-c">
                    <h5>Service Account Details</h5>
                  </Col>
                </Row>
                <br />

                <Row key="ldap_service_account_username">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Bind DN (Optional) &nbsp;
                          <YBInfoTip
                            customClass="ldap-info-popover"
                            title="Bind DN"
                            content="Bind DN will be combined with Base DN and DN Prefix to login to the service account"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="ldap_service_account_username"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="ldap_service_account_password">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Password&nbsp;
                          <YBInfoTip title="Password" content="Password for Service Account">
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="ldap_service_account_password"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                          type="password"
                          autoComplete="new-password"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <br />

                <Row key="footer_banner">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <div className="footer-msg">
                        <Col>
                          <img alt="--" src={Bulb} width="24" />
                        </Col>
                        &nbsp;
                        <Col>
                          <Row>
                            <b>Note!</b> {YUGABYTE_TITLE} will use the following format to connect
                            to your LDAP server.
                          </Row>
                          <Row>
                            {
                              '{{DN Prefix}} + {{LDAP Users Username/Service Account Username}} + {{LDAP Base DN}}'
                            }
                          </Row>
                        </Col>
                      </div>
                    </Row>
                  </Col>
                </Row>

                <br />

                <Row key="ldap_submit">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c ua-action-c">
                    <YBButton
                      btnText="Save"
                      btnType="submit"
                      disabled={isSubmitting || isDisabled || isSaveDisabled}
                      btnClass="btn btn-orange pull-right"
                    />
                  </Col>
                </Row>
              </Form>
            );
          }}
        </Formik>
      </Col>
    </div>
  );
};
