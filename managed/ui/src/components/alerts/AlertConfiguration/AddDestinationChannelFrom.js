import { Field } from 'formik';
import React, { useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { YBModalForm } from '../../common/forms';
import { YBFormInput, YBRadioButtonGroup, YBToggle } from '../../common/forms/fields';

export const AddDestinationChannelFrom = (props) => {
  const { visible, onHide, defaultChannel } = props;

  const [channelType, setChannelType] = useState(defaultChannel);
  const [customSMTP, setCustomSMTP] = useState(true);

  const handleAddDestination = (values) => {
    const { onHide } = props;
    console.log('Form Data -', values);
    onHide();
  };

  const handleOnToggle = (event) => {
    const name = event.target.name;
    const value = event.target.checked;
    if (name === 'customSmtp') {
      setCustomSMTP(!value);
    }
  };

  const handleChannelTypeChange = (event) => {
    setChannelType(event.target.value);
  };

  const getChannelForm = () => {
    switch (channelType) {
      case 'pagerDuty':
        return (
          <>
            <Row>
              <Col lg={12}>
                <Field
                  name="name"
                  type="text"
                  label="Name"
                  placeholder="Enter channel name"
                  component={YBFormInput}
                />
              </Col>
            </Row>
            <Row>
              <Col lg={12}>
                <Field
                  name="serviceKey"
                  type="text"
                  label="PagerDuty Service Integration Key"
                  placeholder="Enter service integration key"
                  component={YBFormInput}
                />
              </Col>
            </Row>
          </>
        );
      case 'slack':
        return (
          <>
            <Row>
              <Col lg={12}>
                <Field
                  name="name"
                  type="text"
                  placeholder="Enter channel name"
                  label="Name"
                  component={YBFormInput}
                />
              </Col>
            </Row>
            <Row>
              <Col lg={12}>
                <Field
                  name="webhookURL"
                  type="text"
                  label="Slack Webhook URL"
                  placeholder="Enter webhook url"
                  component={YBFormInput}
                />
              </Col>
            </Row>
          </>
        );
      case 'email':
        return (
          <>
            <Row>
              <Col lg={12}>
                <Field
                  name="name"
                  type="text"
                  label="Name"
                  placeholder="Enter channel name"
                  component={YBFormInput}
                />
              </Col>
            </Row>
            <Row>
              <Col lg={12}>
                <Field
                  name="emailIds"
                  type="text"
                  label="Emails"
                  placeholder="Enter email addressess"
                  component={YBFormInput}
                />
              </Col>
            </Row>
            <Row>
              <Col lg={12}>
                <Field name="customSmtp">
                  {({ field }) => (
                    <YBToggle
                      onToggle={handleOnToggle}
                      name="customSmtp"
                      input={{
                        value: field.value,
                        onChange: field.onChange
                      }}
                      label="Custom SMTP Configuration"
                      subLabel="Whether or not to use custom SMTP Configuration."
                    />
                  )}
                </Field>
                <div hidden={customSMTP}>
                  <Field
                    name="smtpData.smtpServer"
                    type="text"
                    component={YBFormInput}
                    label="Server"
                    placeholder="SMTP server address"
                  />
                  <Field
                    name="smtpData.smtpPort"
                    type="text"
                    component={YBFormInput}
                    label="Port"
                    placeholder="SMTP server port"
                  />
                  <Field
                    name="smtpData.emailFrom"
                    type="text"
                    component={YBFormInput}
                    label="Email From"
                    placeholder="Send outgoing emails from"
                  />
                  <Field
                    name="smtpData.smtpUsername"
                    type="text"
                    component={YBFormInput}
                    label="Username"
                    placeholder="SMTP server username"
                  />
                  <Field
                    name="smtpData.smtpPassword"
                    type="password"
                    autoComplete="new-password"
                    component={YBFormInput}
                    label="Password"
                    placeholder="SMTP server password"
                  />
                  <Row>
                    <Col lg={6}>
                      <Field name="smtpData.useSSL">
                        {({ field }) => (
                          <YBToggle
                            onToggle={handleOnToggle}
                            name="smtpData.useSSL"
                            input={{
                              value: field.value,
                              onChange: field.onChange
                            }}
                            label="SSL"
                            subLabel="Whether or not to use SSL."
                          />
                        )}
                      </Field>
                    </Col>
                    <Col lg={6}>
                      <Field name="smtpData.useTLS">
                        {({ field }) => (
                          <YBToggle
                            onToggle={handleOnToggle}
                            name="smtpData.useTLS"
                            input={{
                              value: field.value,
                              onChange: field.onChange
                            }}
                            label="TLS"
                            subLabel="Whether or not to use TLS."
                          />
                        )}
                      </Field>
                    </Col>
                  </Row>
                </div>
              </Col>
            </Row>
          </>
        );
      default:
        return null;
    }
  };

  return (
    <YBModalForm
      initialValues={{ ALERT_TARGET_TYPE: channelType }}
      onFormSubmit={handleAddDestination}
      formName={'alertDestinationForm'}
      title="Create a new Alert Destination"
      id="alert-destination-modal"
      visible={visible}
      onHide={onHide}
      submitLabel={'Create'}
    >
      <Row>
        <Col lg={12}>
          <Row>
            <Col lg={8}>
              <div className="form-item-custom-label">Target</div>
              <YBRadioButtonGroup
                name={'ALERT_TARGET_TYPE'}
                options={[
                  { label: 'Email', value: 'email' },
                  { label: 'Slack', value: 'slack' },
                  { label: 'PagerDuty', value: 'pagerDuty' }
                ]}
                onClick={handleChannelTypeChange}
              />
            </Col>
          </Row>
        </Col>
        <hr />
        {getChannelForm()}
      </Row>
    </YBModalForm>
  );
};
