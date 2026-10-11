// Copyright (c) YugabyteDB, Inc.

import { Row, Col } from 'react-bootstrap';
import { Field } from 'redux-form';
import { YBToggle, YBTextInputWithLabel } from '../../common/forms/fields';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import './StorageConfiguration.scss';

const required = (value) => {
  return value ? undefined : 'This field is required.';
};

const OCI_BACKUP_LOCATION_HINT =
  's3://<bucket>[/<path>] or https://objectstorage.<region>.oraclecloud.com/n/<namespace>/b/<bucket>[/<path>] (do not use /o/<object> paths)';

/**
 * Disable/enable input fields while updating the backup storage config.
 *
 * @param {boolean} isEdited Whether the config is being edited.
 * @param {string} configName Input field name.
 * @param {boolean} useOciIam IAM enabled state.
 * @returns {boolean|undefined}
 */
const disableInputFields = (isEdited, configName, useOciIam = false) => {
  if (isEdited && configName === 'OCI_BACKUP_LOCATION') {
    return true;
  }

  if (
    useOciIam &&
    (configName === 'OCI_S3_ACCESS_KEY_ID' ||
      configName === 'OCI_S3_SECRET_ACCESS_KEY' ||
      configName === 'OCI_S3_HOST_BASE')
  ) {
    return true;
  }
};

const OciStorageConfiguration = ({ isEdited, ociIamToggle, useOciIam }) => {
  return (
    <Row className="config-section-header">
      <Col lg={9}>
        <Row className="config-provider-row">
          <Col lg={2}>
            <div className="form-item-custom-label">Configuration Name</div>
          </Col>
          <Col lg={9}>
            <Field
              name="OCI_CONFIGURATION_NAME"
              placeHolder="Configuration Name"
              component={YBTextInputWithLabel}
              validate={required}
              isReadOnly={disableInputFields(isEdited, 'OCI_CONFIGURATION_NAME')}
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
            <div className="form-item-custom-label">Backup Location</div>
          </Col>
          <Col lg={9}>
            <Field
              name="OCI_BACKUP_LOCATION"
              placeHolder={
                useOciIam
                  ? 'https://objectstorage.<region>.oraclecloud.com/n/<namespace>/b/<bucket>'
                  : 's3://bucket_name/prefix'
              }
              component={YBTextInputWithLabel}
              validate={required}
              isReadOnly={disableInputFields(isEdited, 'OCI_BACKUP_LOCATION')}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Backup Location"
              content={`Accepted for both IAM and S3-compatible modes: ${OCI_BACKUP_LOCATION_HINT}.`}
            />
          </Col>
        </Row>
        <Row className="config-provider-row">
          <Col lg={2}>
            <div className="form-item-custom-label">OCI Region</div>
          </Col>
          <Col lg={9}>
            <Field
              name="OCI_REGION"
              placeHolder="us-sanjose-1"
              component={YBTextInputWithLabel}
              validate={required}
              isReadOnly={disableInputFields(isEdited, 'OCI_REGION')}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="OCI Region"
              content="OCI region for Object Storage, e.g. us-sanjose-1."
            />
          </Col>
        </Row>
        <Row className="config-provider-row">
          <Col lg={2}>
            <div className="form-item-custom-label">Use OCI IAM</div>
          </Col>
          <Col lg={9}>
            <Field
              name="USE_OCI_IAM"
              component={YBToggle}
              onToggle={ociIamToggle}
              isReadOnly={disableInputFields(isEdited, 'USE_OCI_IAM')}
              subLabel="Whether to use IAM role for backup on OCI Object Storage."
            />
          </Col>
        </Row>
        <Row className="config-provider-row">
          <Col lg={2}>
            <div className="form-item-custom-label">OCI Namespace</div>
          </Col>
          <Col lg={9}>
            <Field
              name="OCI_NAMESPACE"
              placeHolder="OCI Namespace"
              component={YBTextInputWithLabel}
              validate={useOciIam ? required : undefined}
              isReadOnly={disableInputFields(isEdited, 'OCI_NAMESPACE')}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="OCI Namespace"
              content="OCI Object Storage namespace. Required when using OCI IAM. When using a native HTTPS location, the namespace must match the /n/<namespace>/ segment in the URL."
            />
          </Col>
        </Row>
        <Row className="config-provider-row">
          <Col lg={2}>
            <div className="form-item-custom-label">Access Key</div>
          </Col>
          <Col lg={9}>
            <Field
              name="OCI_S3_ACCESS_KEY_ID"
              placeHolder="OCI S3 Access Key"
              component={YBTextInputWithLabel}
              validate={!useOciIam ? required : undefined}
              isReadOnly={disableInputFields(isEdited, 'OCI_S3_ACCESS_KEY_ID', useOciIam)}
            />
          </Col>
        </Row>
        <Row className="config-provider-row">
          <Col lg={2}>
            <div className="form-item-custom-label">Access Secret</div>
          </Col>
          <Col lg={9}>
            <Field
              name="OCI_S3_SECRET_ACCESS_KEY"
              placeHolder="OCI S3 Access Secret"
              component={YBTextInputWithLabel}
              validate={!useOciIam ? required : undefined}
              isReadOnly={disableInputFields(isEdited, 'OCI_S3_SECRET_ACCESS_KEY', useOciIam)}
            />
          </Col>
        </Row>
        <Row className="config-provider-row">
          <Col lg={2}>
            <div className="form-item-custom-label">S3 Host Base</div>
          </Col>
          <Col lg={9}>
            <Field
              name="OCI_S3_HOST_BASE"
              placeHolder="namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com"
              component={YBTextInputWithLabel}
              validate={!useOciIam ? required : undefined}
              isReadOnly={disableInputFields(isEdited, 'OCI_S3_HOST_BASE', useOciIam)}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="S3 Host Base"
              content="OCI S3-compatible API host base, e.g. namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com. Required when not using OCI IAM."
            />
          </Col>
        </Row>
      </Col>
    </Row>
  );
};

export default OciStorageConfiguration;
