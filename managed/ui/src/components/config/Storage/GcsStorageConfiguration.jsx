// Copyright (c) YugabyteDB, Inc.

import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBToggle, YBTextInputWithLabel, YBPassword } from '../../common/forms/fields';
import { Field } from 'redux-form';
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

  render() {
    const {
      isEdited,
      gcsFederatedIamEnabled,
      showFederatedIam,
      useGcpIam,
    } = this.props;
    // Federation runs on top of an instance identity - the payload sets USE_GCP_IAM either way -
    // so a pasted credentials JSON is unusable under both modes, not just GCP IAM.
    const usesInstanceIdentity = useGcpIam || gcsFederatedIamEnabled;
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
                isReadOnly={this.disableInputFields(isEdited, 'USE_GCP_IAM')}
                subLabel="Whether to use IAM role for backup on GCS."
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip
                title="Use GCP IAM"
                content="Supported for Kubernetes GKE clusters with workload identity."
              />
            </Col>
          </Row>
          {showFederatedIam && (
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">Federated IAM</div>
            </Col>
            <Col lg={9}>
              <Field
                name="GCS_FEDERATED_IAM"
                component={YBToggle}
                isReadOnly={this.disableInputFields(isEdited, 'GCS_FEDERATED_IAM')}
                subLabel="Whether to use cross-cloud federated IAM for GCS backup."
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip
                title="Federated IAM"
                content="Lets YBA reach this GCS bucket from AWS using its own instance identity, for the operations it performs directly such as validating and deleting backups. Leave off when YBA runs on GCP - it already reaches GCS natively. This setting is about YBA only; DB nodes are configured from their provider."
              />
            </Col>
          </Row>
          )}
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">GCS Credentials</div>
            </Col>
            <Col lg={9}>
                <Field
                  name="GCS_CREDENTIALS_JSON"
                  placeHolder="GCS Credentials JSON"
                  component={YBTextInputWithLabel}
                  validate={!usesInstanceIdentity ? required : false}
                  isReadOnly={this.disableInputFields(
                    isEdited,
                    'GCS_CREDENTIALS_JSON',
                    usesInstanceIdentity
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
