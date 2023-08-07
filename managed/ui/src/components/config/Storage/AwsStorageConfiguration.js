// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBToggle, YBTextInputWithLabel, YBPassword } from '../../common/forms/fields';
import { Field } from 'redux-form';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import './StorageConfiguration.scss';

const required = (value) => {
  return value ? undefined : 'This field is required.';
};

const proxyFieldsRequired = (value, data) => {
  if (
    !value &&
    (data?.PROXY_SETTINGS?.PROXY_HOST ||
      data?.PROXY_SETTINGS?.PROXY_PORT ||
      data?.PROXY_SETTINGS?.PROXY_USERNAME ||
      data?.PROXY_SETTINGS?.PROXY_PASSWORD)
  )
    return 'This field is required.';
  return undefined;
};

const proxyPasswordRequired = (value, data) => {
  if (!value && data?.PROXY_SETTINGS?.PROXY_USERNAME) return 'This field is required.';
  return undefined;
};

class AwsStorageConfiguration extends Component {
  /**
   * This method will help us to disable/enable the input fields
   * based while updating the backup storage config.
   *
   * @param {object} data Respective row deatils.
   * @param {string} configName Input field name.
   * @param {boolean} iamRoleEnabled IAM enabled state.
   * @returns true
   */
  disableInputFields = (isEdited, configName, iamRoleEnabled = false, inUse) => {
    if (isEdited && (configName === 'S3_BACKUP_LOCATION' || configName === 'AWS_HOST_BASE')) {
      return true;
    }

    if (
      iamRoleEnabled &&
      (configName === 'AWS_ACCESS_KEY_ID' || configName === 'AWS_SECRET_ACCESS_KEY')
    ) {
      return true;
    }
  };

  componentDidMount = () => {
    const { customerConfigs } = this.props;
    const s3Config = customerConfigs?.data.find((config) => config.name === 'S3');
    const config = s3Config ? s3Config.data : {};
    if (isNonEmptyObject(config) && config.IAM_INSTANCE_PROFILE === 'true') {
      this.setState({ iamRoleEnabled: true });
    }
  };

  render() {
    const {
      isEdited,
      iamInstanceToggle,
      iamRoleEnabled,
      enablePathStyleAccess,
      enableS3BackupProxy
    } = this.props;
    return (
      <Row className="config-section-header">
        <Col lg={9}>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">Configuration Name</div>
            </Col>
            <Col lg={9}>
              <Field
                name="S3_CONFIGURATION_NAME"
                placeHolder="Configuration Name"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={this.disableInputFields(isEdited, 'S3_CONFIGURATION_NAME')}
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip
                title="Configuration Name"
                content="The backup configuration name is required."
              />
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">IAM Role</div>
            </Col>
            <Col lg={9}>
              <Field
                name="IAM_INSTANCE_PROFILE"
                component={YBToggle}
                onToggle={iamInstanceToggle}
                isReadOnly={this.disableInputFields(isEdited, 'IAM_INSTANCE_PROFILE')}
                subLabel="Whether to use instance's IAM role for S3 backup."
              />
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">Access Key</div>
            </Col>
            <Col lg={9}>
              {iamRoleEnabled ? (
                <Field
                  name="AWS_ACCESS_KEY_ID"
                  placeHolder="AWS Access Key"
                  component={YBTextInputWithLabel}
                  isReadOnly={this.disableInputFields(
                    isEdited,
                    'AWS_ACCESS_KEY_ID',
                    iamRoleEnabled
                  )}
                />
              ) : (
                <Field
                  name="AWS_ACCESS_KEY_ID"
                  placeHolder="AWS Access Key"
                  component={YBTextInputWithLabel}
                  validate={required}
                  isReadOnly={this.disableInputFields(
                    isEdited,
                    'AWS_ACCESS_KEY_ID',
                    iamRoleEnabled
                  )}
                />
              )}
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">Access Secret</div>
            </Col>
            <Col lg={9}>
              {iamRoleEnabled ? (
                <Field
                  name="AWS_SECRET_ACCESS_KEY"
                  placeHolder="AWS Access Secret"
                  component={YBTextInputWithLabel}
                  isReadOnly={this.disableInputFields(
                    isEdited,
                    'AWS_SECRET_ACCESS_KEY',
                    iamRoleEnabled
                  )}
                />
              ) : (
                <Field
                  name="AWS_SECRET_ACCESS_KEY"
                  placeHolder="AWS Access Secret"
                  component={YBTextInputWithLabel}
                  validate={required}
                  isReadOnly={this.disableInputFields(
                    isEdited,
                    'AWS_SECRET_ACCESS_KEY',
                    iamRoleEnabled
                  )}
                />
              )}
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">S3 Bucket</div>
            </Col>
            <Col lg={9}>
              <Field
                name="S3_BACKUP_LOCATION"
                placeHolder="s3://bucket_name"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={this.disableInputFields(isEdited, 'S3_BACKUP_LOCATION')}
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip title="S3 Bucket" content="S3 bucket format: s3://bucket_name." />
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">S3 Bucket Host Base</div>
            </Col>
            <Col lg={9}>
              <Field
                name="AWS_HOST_BASE"
                placeHolder="s3.amazonaws.com"
                component={YBTextInputWithLabel}
                isReadOnly={this.disableInputFields(isEdited, 'AWS_HOST_BASE')}
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip
                title="S3 Host"
                content="Host of S3 bucket. Defaults to s3.amazonaws.com"
              />
            </Col>
          </Row>
          {enablePathStyleAccess && (
            <Row className="config-provider-row">
              <Col lg={2}>
                <div className="form-item-custom-label">S3 Path Style Access</div>
              </Col>
              <Col lg={9}>
                {['true', 'false'].map((target) => (
                  <span className="btn-group btn-group-radio form-radio-values" key={target}>
                    <Field
                      name="PATH_STYLE_ACCESS"
                      type="radio"
                      component="input"
                      value={target}
                      isReadOnly={this.disableInputFields(isEdited, 'PATH_STYLE_ACCESS')}
                    />
                    &nbsp;{target}
                  </span>
                ))}
              </Col>
            </Row>
          )}

          {enableS3BackupProxy && (
            <Row className="backup-proxy-config">
              <Row className="config-provider-row">
                <h4>Proxy Configuration</h4>
              </Row>
              <div className="divider"></div>

              <Row className="config-provider-row">
                <Col lg={2}>
                  <div className="form-item-custom-label">Host</div>
                </Col>
                <Col lg={9}>
                  <Field
                    validate={proxyFieldsRequired}
                    name="PROXY_SETTINGS.PROXY_HOST"
                    placeHolder="Proxy Host"
                    component={YBTextInputWithLabel}
                  />
                </Col>
                <Col lg={1} className="config-zone-tooltip">
                  <YBInfoTip title="Host" content="Host address of the proxy server" />
                </Col>
              </Row>

              <Row className="config-provider-row">
                <Col lg={2}>
                  <div className="form-item-custom-label">Port</div>
                </Col>
                <Col lg={9}>
                  <Field
                    validate={proxyFieldsRequired}
                    name="PROXY_SETTINGS.PROXY_PORT"
                    placeHolder="Proxy Port"
                    type="number"
                    component={YBTextInputWithLabel}
                  />
                </Col>
                <Col lg={1} className="config-zone-tooltip">
                  <YBInfoTip
                    title="Port"
                    content="Port number at which the proxy server is running"
                  />
                </Col>
              </Row>

              <Row className="config-provider-row">
                <Col lg={2}>
                  <div className="form-item-custom-label">Username (Optional)</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="PROXY_SETTINGS.PROXY_USERNAME"
                    placeHolder="Proxy Username"
                    component={YBTextInputWithLabel}
                  />
                </Col>
                <Col lg={1} className="config-zone-tooltip">
                  <YBInfoTip title="Username" content="Username for authentication" />
                </Col>
              </Row>

              <Row className="config-provider-row">
                <Col lg={2}>
                  <div className="form-item-custom-label">Password (Optional)</div>
                </Col>
                <Col lg={9}>
                  <Field
                    validate={proxyPasswordRequired}
                    name="PROXY_SETTINGS.PROXY_PASSWORD"
                    placeHolder="Proxy Password"
                    component={YBPassword}
                    type="password"
                    autocomplete="new-password"
                  />
                </Col>
                <Col lg={1} className="config-zone-tooltip">
                  <YBInfoTip title="Password" content="Password for authentication" />
                </Col>
              </Row>
            </Row>
          )}
        </Col>
      </Row>
    );
  }
}

export default AwsStorageConfiguration;
