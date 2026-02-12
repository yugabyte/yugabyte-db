// Copyright (c) YugabyteDB, Inc.

import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field } from 'redux-form';
import { YBToggle, YBTextInputWithLabel, YBPassword } from '../../common/forms/fields';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import './StorageConfiguration.scss';

const required = (value) => {
  return value ? undefined : 'This field is required.';
};

class AzureStorageConfiguration extends Component {
  /**
   * This method will help us to disable/enable the input fields
   * based while updating the backup storage config.
   *
   * @param {object} data Respective row deatils.
   * @param {string} configName Input field name.
   * @param {boolean} useAzureIam IAM enabled state.
   * @returns true
   */
  disableInputFields = (isEdited, configName, useAzureIam = false) => {
    if (isEdited && configName === 'AZ_BACKUP_LOCATION') {
      return true;
    }

    if (
      useAzureIam &&
      (configName === 'AZURE_STORAGE_SAS_TOKEN')
    ) {
      return true;
    }
  };

  componentDidMount = () => {
    const { customerConfigs } = this.props;
    const azureConfig = customerConfigs?.data.find((config) => config.name === 'AZ');
    this.setState({useAzureIam: azureConfig?.data?.USE_AZURE_IAM === 'true'});
  };

  render() {
    const {
      isEdited,
      azureIamToggle,
      useAzureIam,
    } = this.props;
    return (
      <Row className="config-section-header">
        <Col lg={8}>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">Configuration Name</div>
            </Col>
            <Col lg={9}>
              <Field
                name="AZ_CONFIGURATION_NAME"
                placeHolder="Configuration Name"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={this.disableInputFields(isEdited, 'AZ_CONFIGURATION_NAME')}
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
              <div className="form-item-custom-label">Container URL</div>
            </Col>
            <Col lg={9}>
              <Field
                name="AZ_BACKUP_LOCATION"
                placeHolder="Container URL"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={this.disableInputFields(isEdited, 'AZ_BACKUP_LOCATION')}
              />
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">Use Azure IAM</div>
            </Col>
            <Col lg={9}>
              <Field
                name="USE_AZURE_IAM"
                component={YBToggle}
                onToggle={azureIamToggle}
                isReadOnly={this.disableInputFields(isEdited, 'USE_AZURE_IAM')}
                subLabel="Whether to use IAM role for backup on Azure."
              />
            </Col>
          </Row>
          <Row className="config-provider-row">
            <Col lg={2}>
              <div className="form-item-custom-label">SAS Token</div>
            </Col>
            <Col lg={9}>
                <Field
                  name="AZURE_STORAGE_SAS_TOKEN"
                  placeHolder="SAS Token"
                  component={YBTextInputWithLabel}
                  validate={!useAzureIam ? required: false}
                  isReadOnly={this.disableInputFields(
                    isEdited,
                    'AZURE_STORAGE_SAS_TOKEN',
                    useAzureIam
                  )}
                />
            </Col>
          </Row>
        </Col>
      </Row>
    );
  }
}

export default AzureStorageConfiguration;
