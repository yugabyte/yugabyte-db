// Copyright (c) YugaByte, Inc.

import React, { useEffect, useState } from 'react';
import * as Yup from 'yup';
import { trimStart, trimEnd } from 'lodash';
import { toast } from 'react-toastify';
import { Row, Col, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { Formik, Form, Field } from 'formik';
import { YBFormInput, YBButton, YBModal, YBToggle } from '../../common/forms/fields';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { setSSO } from '../../../config';
import WarningIcon from '../icons/warning_icon';

const VALIDATION_SCHEMA = Yup.object().shape({
  discoveryURI: Yup.string().required('Discovery URL is required'),
  clientID: Yup.string().required('Client ID is required'),
  secret: Yup.string().required('Client Secret is required')
});

const OIDC_PATH = 'yb.security';
const OIDC_FIELDS = [
  'use_oauth',
  'type',
  'clientID',
  'secret',
  'discoveryURI',
  'oidcScope',
  'oidcEmailAttribute'
];

const TOAST_OPTIONS = { autoClose: 1750 };

export const OIDCAuth = (props) => {
  const {
    fetchRunTimeConfigs,
    setRunTimeConfig,
    deleteRunTimeConfig,
    runtimeConfigs: {
      data: { configEntries }
    }
  } = props;
  const [showToggle, setToggleVisible] = useState(false);
  const [dialog, showDialog] = useState(false);
  const [oidcEnabled, setOIDC] = useState(false);

  const transformData = (values) => {
    const transformedData = {
      ...values,
      type: 'OIDC'
    };

    return transformedData;
  };

  const escapeStr = (str) => {
    let s = trimStart(str, '""');
    s = trimEnd(s, '""');
    return s;
  };

  const initializeFormValues = () => {
    const oidcFields = OIDC_FIELDS.map((ef) => `${OIDC_PATH}.${ef}`);
    const oidcConfigs = configEntries.filter((config) => oidcFields.includes(config.key));
    const formData = oidcConfigs.reduce((fData, config) => {
      const [, key] = config.key.split(`${OIDC_PATH}.`);
      fData[key] = escapeStr(config.value);
      return fData;
    }, {});

    const finalFormData = {
      ...formData
    };

    return finalFormData;
  };

  const saveOIDCConfigs = async (values) => {
    const formValues = transformData(values);
    const initValues = initializeFormValues();
    const promiseArray = Object.keys(formValues).reduce((promiseArr, key) => {
      if (formValues[key] !== initValues[key]) {
        promiseArr.push(
          formValues[key] !== ''
            ? setRunTimeConfig({
                key: `${OIDC_PATH}.${key}`,
                value: formValues[key]
              })
            : deleteRunTimeConfig({
                key: `${OIDC_PATH}.${key}`
              })
        );
      }

      return promiseArr;
    }, []);

    try {
      toast.warn('Please wait. OIDC configuration is getting saved', TOAST_OPTIONS);
      await Promise.all(promiseArray);

      //set oauth after all other configs are saved - to avoid other configs fail
      const response = await setRunTimeConfig({
        key: `${OIDC_PATH}.use_oauth `,
        value: true
      });
      setSSO(true);
      response && fetchRunTimeConfigs();
      toast.success('OIDC configuration is saved successfully', TOAST_OPTIONS);
    } catch {
      toast.error('Failed to save OIDC configuration', TOAST_OPTIONS);
    }
  };

  const handleToggle = async (e) => {
    const value = e.target.checked;

    if (!value) showDialog(true);
    else {
      setOIDC(true);
      await setRunTimeConfig({
        key: `${OIDC_PATH}.use_oauth`,
        value: true
      });
      setSSO(true);
      toast.success(`OIDC authentication is enabled`, TOAST_OPTIONS);
    }
  };

  const handleDialogClose = () => {
    showDialog(false);
  };

  const handleDialogSubmit = async () => {
    setOIDC(false);
    showDialog(false);
    await setRunTimeConfig({
      key: `${OIDC_PATH}.use_oauth`,
      value: 'false'
    });
    setSSO(false);
    toast.warn(`OIDC authentication is disabled`, TOAST_OPTIONS);
  };

  useEffect(() => {
    const oidcConfig = configEntries.find((config) =>
      config.key.includes(`${OIDC_PATH}.use_oauth`)
    );
    setToggleVisible(!!oidcConfig);
    setOIDC(escapeStr(oidcConfig?.value) === 'true');
  }, [configEntries, setToggleVisible, setOIDC]);

  return (
    <div className="bottom-bar-padding">
      <YBModal
        title="Disable OIDC"
        visible={dialog}
        showCancelButton={true}
        submitLabel="Disable OIDC"
        cancelLabel="Cancel"
        cancelBtnProps={{
          className: 'btn btn-default pull-left oidc-cancel-btn'
        }}
        onHide={handleDialogClose}
        onFormSubmit={handleDialogSubmit}
      >
        <div className="oidc-modal-c">
          <div className="oidc-modal-c-icon">
            <WarningIcon />
          </div>
          <div className="oidc-modal-c-content">
            <b>Note!</b> By disabling OIDC users won't be able to login with your current
            authentication provider. Are you sure?
          </div>
        </div>
      </YBModal>
      <Col>
        <Formik
          validationSchema={VALIDATION_SCHEMA}
          initialValues={initializeFormValues()}
          enableReinitialize
          onSubmit={(values, { setSubmitting, resetForm }) => {
            saveOIDCConfigs(values);
            setSubmitting(false);
            resetForm(values);
          }}
        >
          {({ handleSubmit, isSubmitting, errors, dirty, values }) => {
            const isDisabled = !oidcEnabled && showToggle;
            const isSaveDisabled = !dirty;

            const OIDCToggle = () => (
              <YBToggle
                onToggle={handleToggle}
                name="use_oauth"
                input={{
                  value: oidcEnabled,
                  onChange: () => {}
                }}
                isReadOnly={!showToggle}
              />
            );

            const OIDCToggleTooltip = () => (
              <OverlayTrigger
                placement="top"
                overlay={
                  <Tooltip className="high-index" id="oidc-toggle-tooltip">
                    To enable OIDC you need to provide and save the required configurations
                  </Tooltip>
                }
              >
                <div>
                  <OIDCToggle />
                </div>
              </OverlayTrigger>
            );

            return (
              <Form name="OIDCConfigForm" onSubmit={handleSubmit}>
                <Row className="ua-field-row">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c ua-title-c"></Col>

                      <Col className="ua-toggle-c">
                        <>
                          <Col className="ua-toggle-label-c">
                            OIDC Enabled &nbsp;
                            <YBInfoTip
                              title="OIDC Enabled"
                              content="Enable or Disable OIDC Authentication"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </Col>

                          {showToggle ? <OIDCToggle /> : <OIDCToggleTooltip />}
                        </>
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="oidc_clientID">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Client ID &nbsp;
                          <YBInfoTip
                            title="Client ID"
                            content="The unique identifier of your manually created client application in the Identity Provider"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="clientID"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="oidc_secret">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Client Secret &nbsp;
                          <YBInfoTip
                            customClass="oidc-info-popover"
                            title="Client Secret"
                            content="The password or secret for authenticating your Yugabyte client application with your Identity Provider"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="secret"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="oidc_discoveryURI">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Discovery URL&nbsp;
                          <YBInfoTip
                            title="Discovery URL"
                            content="Endpoint that validates all authorization requests. This can be found in discovery document"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="discoveryURI"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="oidc_scope">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Scope&nbsp;
                          <YBInfoTip title="Scope" content="Identity provider scope">
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="oidcScope"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <Row key="oidc_email_attribute">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c">
                        <div>
                          Email Attribute&nbsp;
                          <YBInfoTip
                            title="Email Attribute"
                            content="Scope containing email ID of the user"
                          >
                            <i className="fa fa-info-circle" />
                          </YBInfoTip>
                        </div>
                      </Col>
                      <Col lg={12} className="ua-field">
                        <Field
                          name="oidcEmailAttribute"
                          component={YBFormInput}
                          disabled={isDisabled}
                          className="ua-form-field"
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>

                <br />

                <Row key="oidc_submit">
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
