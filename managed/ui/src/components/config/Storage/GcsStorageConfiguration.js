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

class GcsStorageConfiguration extends Component {
  /**
   * This method will help us to disable/enable the input fields
   * based while updating the backup storage config.
   *
   * @param {object} data Respective row deatils.
   * @param {string} configName Input field name.
   * @param {boolean} useGcpIam IAM enabled state.
   * @returns true
   */
  disableInputFields = (isEdited, configName, useGcpIam = false) => {
    if (isEdited && configName === 'GCS_BACKUP_LOCATION') {
      return true;
    }

    if (
      useGcpIam &&
      (configName === 'GCS_CREDENTIALS_JSON')
    ) {
      return true;
    }
  };

  componentDidMount = () => {
    const { customerConfigs } = this.props;
    const gcsConfig = customerConfigs?.data.find((config) => config.name === 'GCS');
    this.setState({useGcpIam: gcsConfig?.data?.USE_GCP_IAM === 'true'});
  };

  render() {
    const {
      isEdited,
      gcpIamToggle,
      useGcpIam,
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
                name="GCS_CONFIGURATION_NAME"
                placeHolder="Configuration Name"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={this.disableInputFields(isEdited, 'GCS_CONFIGURATION_NAME')}
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
              <div className="form-item-custom-label">GCS Bucket</div>
            </Col>
            <Col lg={9}>
              <Field
                name="GCS_BACKUP_LOCATION"
                placeHolder="GCS Bucket"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={this.disableInputFields(isEdited, 'GCS_BACKUP_LOCATION')}
              />
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">Use GCP IAM</div>
            </Col>
            <Col lg={9}>
              <Field
                name="USE_GCP_IAM"
                component={YBToggle}
                onToggle={gcpIamToggle}
                isReadOnly={this.disableInputFields(isEdited, 'USE_GCP_IAM')}
                subLabel="Whether to use GKE Service Account for backup."
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip
                title="Use GCP IAM"
                content="Supported for Kubernetes GKE clusters with workload identity."
              />
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">GCS Credentials</div>
            </Col>
            <Col lg={9}>
                <Field
                  name="GCS_CREDENTIALS_JSON"
                  placeHolder="GCS Credentials JSON"
                  component={YBTextInputWithLabel}
                  validate={!useGcpIam ? required: false}
                  isReadOnly={this.disableInputFields(
                    isEdited,
                    'GCS_CREDENTIALS_JSON',
                    useGcpIam
                  )}
                />
            </Col>
          </Row>
        </Col>
      </Row>
    );
  }
}

export default GcsStorageConfiguration;
