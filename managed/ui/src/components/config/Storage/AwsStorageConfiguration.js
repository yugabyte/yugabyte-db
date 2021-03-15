// Copyright (c) YugaByte, Inc.

import React, { Component } from "react";
import { Row, Col } from "react-bootstrap";
import { YBToggle, YBTextInputWithLabel } from "../../common/forms/fields";
import { Field } from "redux-form";
import { isEmptyObject, isDefinedNotNull } from "../../../utils/ObjectUtils";
import YBInfoTip from "../../common/descriptors/YBInfoTip";

const required = value => value ? undefined : "This field is required.";

/**
 * This method is used to validate the Access and the Secret key.
 * 
 * @param {any} value Input value.
 * @param {boolean} iamRoleEnabled IAM enabled state.
 * @returns "This field is required."
 */
const validateKeys = (value, iamRoleEnabled) => {
  if (!isDefinedNotNull(value) && !iamRoleEnabled) {
    return "This field is required.";
  }
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
  disableInputFields = (data, configName, iamRoleEnabled) => {
    if (!isEmptyObject(data)) {
      if (data.inUse) {
        if (configName !== "S3_CONFIGURATION_NAME") {
          return true;
        }
      }
    }

    if (iamRoleEnabled) {
      if (configName === "AWS_ACCESS_KEY_ID" || "AWS_SECRET_ACCESS_KEY") {
        return true;
      }
    }
  };

  render() {
    const {
      data,
      iamInstanceToggle,
      iamRoleEnabled
    } = this.props;

    return (
      <Row className="config-section-header" key={"s3"}>
        <Col lg={9}>
          <Row className="config-provider-row" key={"configuration-name"}>
            <Col lg={2}>
              <div className="form-item-custom-label">Configuration Name</div>
            </Col>
            <Col lg={9}>
              <Field
                name="S3_CONFIGURATION_NAME"
                placeHolder="Configuration Name"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={this.disableInputFields(data, "S3_CONFIGURATION_NAME")}
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip
                title="Configuration Name"
                content="The backup configuration name is required."
              />
            </Col>
          </Row>
          <Row className="config-provider-row" key={"s3-iam-instance-profile"}>
            <Col lg={2}>
              <div className="form-item-custom-label">IAM Role</div>
            </Col>
            <Col lg={9}>
              <Field
                name="IAM_INSTANCE_PROFILE"
                component={YBToggle}
                onToggle={iamInstanceToggle}
                isReadOnly={this.disableInputFields(data, "IAM_INSTANCE_PROFILE")}
                subLabel="Whether to use instance's IAM role for S3 backup."
              />
            </Col>
          </Row>
          <Row className="config-provider-row" key={"s3-aws-access-key-id"}>
            <Col lg={2}>
              <div className="form-item-custom-label">Access Key</div>
            </Col>
            <Col lg={9}>
              <Field
                name="AWS_ACCESS_KEY_ID"
                placeHolder="AWS Access Key"
                component={YBTextInputWithLabel}
                validate={(value) => validateKeys(value, iamRoleEnabled)}
                isReadOnly={this.disableInputFields(data, "AWS_ACCESS_KEY_ID", iamRoleEnabled)}
              />
            </Col>
          </Row>
          <Row className="config-provider-row" key={"s3-aws-secret-access-key"}>
            <Col lg={2}>
              <div className="form-item-custom-label">Access Secret</div>
            </Col>
            <Col lg={9}>
              <Field
                name="AWS_SECRET_ACCESS_KEY"
                placeHolder="AWS Access Secret"
                component={YBTextInputWithLabel}
                validate={(value) => validateKeys(value, iamRoleEnabled)}
                isReadOnly={this.disableInputFields(data, "AWS_SECRET_ACCESS_KEY", iamRoleEnabled)}
              />
            </Col>
          </Row>
          <Row className="config-provider-row" key={"s3-backup-location"}>
            <Col lg={2}>
              <div className="form-item-custom-label">S3 Bucket</div>
            </Col>
            <Col lg={9}>
              <Field
                name="S3_BACKUP_LOCATION"
                placeHolder="s3://S3 Bucket"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={this.disableInputFields(data, "S3_BACKUP_LOCATION")}
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip
                title="S3 Bucket"
                content="S3 bucket format: s3://bucket_name."
              />
            </Col>
          </Row>
          <Row className="config-provider-row" key={"s3-backup-host-base"}>
            <Col lg={2}>
              <div className="form-item-custom-label">S3 Bucket Host Base</div>
            </Col>
            <Col lg={9}>
              <Field
                name="AWS_HOST_BASE"
                placeHolder="s3.amazonaws.com"
                component={YBTextInputWithLabel}
                isReadOnly={this.disableInputFields(data, "AWS_HOST_BASE")}
              />
            </Col>
            <Col lg={1} className="config-zone-tooltip">
              <YBInfoTip
                title="S3 Host"
                content="Host of S3 bucket. Defaults to s3.amazonaws.com"
              />
            </Col>
          </Row>
        </Col>
      </Row>
    );
  }
}

export default AwsStorageConfiguration;
