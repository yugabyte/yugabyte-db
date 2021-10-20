import { Field } from 'formik';
import React, { useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { YBModalForm } from '../../common/forms';
import { YBControlledSelectWithLabel, YBFormInput, YBToggle } from '../../common/forms/fields';
import * as Yup from 'yup';

import './AddDestinationChannelForm.scss';

export const AddDestinationChannelForm = (props) => {
  const { visible, onHide, onError, defaultChannel } = props;
  const [channelType, setChannelType] = useState(defaultChannel);
  const [customSMTP, setCustomSMTP] = useState(props.customSmtp ? props.customSmtp : false);
  const [defaultRecipients, setDefaultRecipients] = useState(
    props.defaultRecipients ? props.defaultRecipients : false
  );

  // TODO: Add option for pagerDuty once API is avaialable
  const channelTypeList = [
    <option key={1} value="email">
      Email
    </option>,
    <option key={2} value="slack">
      Slack
    </option>
  ];
  /**
   * Hide the modal after setting the custom smtp flag to false.
   */
  const onModalHide = () => {
    setChannelType(defaultChannel);
    setCustomSMTP(false);
    setDefaultRecipients(false);
    onHide();
  };

  /**
   * Create the payload based on channel type and add the channel.
   * @param {FormValues} values
   */
  const handleAddDestination = (values, setSubmitting) => {
    const payload = {
      name: '',
      params: {}
    };

    switch (values.CHANNEL_TYPE) {
      case 'slack':
        payload['name'] = values['slack_name'];
        payload['params']['channelType'] = 'Slack';
        payload['params']['webhookUrl'] = values.webhookURL;
        payload['params']['username'] = values['slack_name'];
        break;
      case 'email':
        payload['name'] = values['email_name'];
        payload['params']['channelType'] = 'Email';
        if (!defaultRecipients) {
          payload['params']['recipients'] = values.emailIds.split(',');
        } else {
          payload['params']['defaultRecipients'] = true;
        }
        if (customSMTP) {
          payload['params']['smtpData'] = values.smtpData;
          payload['params']['smtpData']['useSSL'] = values.smtpData.useSSL || false;
          payload['params']['smtpData']['useTLS'] = values.smtpData.useTLS || false;
        } else {
          payload['params']['defaultSmtpSettings'] = true;
        }
        break;
      default:
        break;
    }

    if (props.type === 'edit') {
      try {
        props.editAlertChannel(values['uuid'], payload);
        setSubmitting(false);
      } catch (err) {
        onError();
      }
    } else {
      try {
        props.createAlertChannel(payload).then(() => {
          props.getAlertChannels().then((channels) => {
            channels = channels.map((channel) => {
              return {
                value: channel['uuid'],
                label: channel['name']
              };
            });
            props.updateDestinationChannel(channels);
          });
        });
        onModalHide();
      } catch (err) {
        if (onError) {
          onError();
        }
      }
    }
  };

  const handleOnToggle = (event) => {
    const name = event.target.name;
    const value = event.target.checked;
    if (name === 'customSmtp') {
      setCustomSMTP(value);
    }
    if (name === 'defaultRecipients') {
      setDefaultRecipients(value);
    }
  };

  const handleChannelTypeChange = (event) => {
    setChannelType(event.target.value);
  };

  const validationSchemaEmail = Yup.object().shape({
    email_name: Yup.string().required('Name is Required'),
    emailIds: !defaultRecipients && Yup.string().required('Email id is Required')
  });

  const validationSchemaSlack = Yup.object().shape({
    slack_name: Yup.string().required('Slack name is Required'),
    webhookURL: Yup.string().required('Web hook Url is Required')
  });

  const getChannelForm = () => {
    switch (channelType) {
      case 'pagerDuty':
        return (
          <>
            <Row>
              <Col lg={12}>
                <Field
                  name="pagerDuty_name"
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
                  name="slack_name"
                  type="text"
                  placeholder="Enter username or channel name"
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
                  name="email_name"
                  type="text"
                  label="Name"
                  placeholder="Enter channel name"
                  component={YBFormInput}
                />
              </Col>
            </Row>
            <Row>
              <Col lg={12}>
                <Row className="component-flex">
                  <Col lg={1} className="noLeftPadding">
                    <Field name="defaultRecipients">
                      {({ field }) => (
                        <YBToggle
                          onToggle={handleOnToggle}
                          name="defaultRecipients"
                          input={{
                            value: field.value,
                            onChange: field.onChange
                          }}
                        />
                      )}
                    </Field>
                  </Col>
                  <Col lg={11} className="component-label">
                    <strong>Use Default Recipients</strong>
                  </Col>
                </Row>

                {!defaultRecipients && (
                  <Field
                    name="emailIds"
                    type="text"
                    label="Emails"
                    placeholder="Enter email addressess"
                    component={YBFormInput}
                  />
                )}
              </Col>
            </Row>
            <Row>
              <Col lg={12}>
                <Row className="component-flex">
                  <Col lg={1} className="noLeftPadding">
                    <Field name="customSmtp">
                      {({ field }) => (
                        <YBToggle
                          onToggle={handleOnToggle}
                          name="customSmtp"
                          input={{
                            value: field.value,
                            onChange: field.onChange
                          }}
                        />
                      )}
                    </Field>
                  </Col>
                  <Col lg={11} className="component-label">
                    <strong>Custom SMTP Configuration</strong>
                  </Col>
                </Row>

                {customSMTP && (
                  <>
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
                      <Col lg={6} className="noLeftPadding">
                        <Row className="component-flex">
                          <Col lg={3} className="noLeftPadding">
                            <Field name="smtpData.useSSL">
                              {({ field }) => (
                                <YBToggle
                                  onToggle={handleOnToggle}
                                  name="smtpData.useSSL"
                                  input={{
                                    value: field.value,
                                    onChange: field.onChange
                                  }}
                                />
                              )}
                            </Field>
                          </Col>
                          <Col lg={9} className="noLeftPadding component-label">
                            <strong>Use SSL</strong>
                          </Col>
                        </Row>
                      </Col>
                      <Col lg={6} className="noLeftPadding">
                        <Row className="component-flex">
                          <Col lg={3} className="noLeftPadding">
                            <Field name="smtpData.useTLS">
                              {({ field }) => (
                                <YBToggle
                                  onToggle={handleOnToggle}
                                  name="smtpData.useTLS"
                                  input={{
                                    value: field.value,
                                    onChange: field.onChange
                                  }}
                                />
                              )}
                            </Field>
                          </Col>
                          <Col lg={9} className="noLeftPadding component-label">
                            <strong>Use TLS</strong>
                          </Col>
                        </Row>
                      </Col>
                    </Row>
                  </>
                )}
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
      formName="alertDestinationForm"
      title={`${props.type === 'edit' ? 'Edit' : 'Create new'} alert channel`}
      id="alert-destination-modal"
      visible={visible}
      onHide={onModalHide}
      initialValues={props.editValues || {}}
      submitLabel={props.type === 'edit' ? 'Edit' : 'Create'}
      validationSchema={channelType === 'email' ? validationSchemaEmail : validationSchemaSlack}
      onFormSubmit={(values, { setSubmitting }) => {
        const payload = {
          ...values,
          CHANNEL_TYPE: channelType
        };

        handleAddDestination(payload, setSubmitting);
      }}
    >
      <Row>
        <Row>
          <Col lg={8}>
            <div className="form-item-custom-label">Target</div>
            <YBControlledSelectWithLabel
              name="CHANNEL_TYPE"
              options={channelTypeList}
              selectVal={channelType}
              onInputChanged={handleChannelTypeChange}
            />
          </Col>
        </Row>
        <hr />
        {getChannelForm()}
      </Row>
    </YBModalForm>
  );
};
