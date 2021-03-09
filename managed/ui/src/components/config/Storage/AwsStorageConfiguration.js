// Copyright (c) YugaByte, Inc.

import React, { Component } from "react";
import { Row, Col } from "react-bootstrap";
import { YBButton, YBToggle, YBTextInputWithLabel } from "../../common/forms/fields";
import { Field } from "redux-form";
import { YBConfirmModal } from "../../modals";
import { isDefinedNotNull, isEmptyObject } from "../../../utils/ObjectUtils";
import YBInfoTip from "../../common/descriptors/YBInfoTip";

const required = value => value ? undefined : "This field is required.";

class AwsStorageConfiguration extends Component {

  // This method will help us to disable the input fields
  // based on the given conditions.
  disableInputFields = (data, configName, iamRoleEnabled) => {
    if (data.inUse) {
      if (configName !== "S3_CONFIGURATION_NAME") {
        return true;
      }
    }

    if (data["IAM_INSTANCE_PROFILE"] || iamRoleEnabled) {
      if (configName === "AWS_ACCESS_KEY_ID" || configName === "AWS_SECRET_ACCESS_KEY") {
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
    const allowKeyEdits = !isEmptyObject(data) || iamRoleEnabled;
    
    return (
      <Row className="config-section-header" key={"s3"}>
        <Col lg={9}>
          <Row className="config-provider-row" key={"configuration-name"}>
            <Col lg={2}>
              <div className="form-item-custom-label">Configuration Name</div>
            </Col>
            <Col lg={9}>
              {/* {!isEmptyObject(data) ? (
                <Field
                  name="AWS_CONFIGURATION_NAME"
                  component={YBTextInputWithLabel}
                  // input={{
                  //   value: data["CONFIGURATION_NAME"],
                  //   disabled: this.disableInputFields(data, "S3_CONFIGURATION_NAME")
                  // }}
                />
              ) : ( */}
                <Field
                  name="S3_CONFIGURATION_NAME"
                  placeHolder="Configuration Name"
                  component={YBTextInputWithLabel}
                  validate={required}
                />
              {/* )} */}
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
              {!isEmptyObject(data) ? (
                <Field
                  name="IAM_INSTANCE_PROFILE"
                  component={YBToggle}
                  onToggle={iamInstanceToggle}
                  input={{
                    value: data["IAM_INSTANCE_PROFILE"],
                    disabled: this.disableInputFields(data, "IAM_INSTANCE_PROFILE")
                  }}
                  subLabel="Whether to use instance's IAM role for S3 backup."
                />
              ) : (
                <Field
                  name="IAM_INSTANCE_PROFILE"
                  component={YBToggle}
                  onToggle={iamInstanceToggle}
                  subLabel="Whether to use instance's IAM role for S3 backup."
                />
              )}
            </Col>
          </Row>
          <Row className="config-provider-row" key={"s3-aws-access-key-id"}>
            <Col lg={2}>
              <div className="form-item-custom-label">Access Key</div>
            </Col>
            <Col lg={9}>
              {allowKeyEdits ? (
                <Field
                  name="AWS_ACCESS_KEY_ID"
                  placeHolder="AWS Access Key"
                  input={{
                    value: data["AWS_ACCESS_KEY_ID"] || "",
                    disabled: this.disableInputFields(data, "AWS_ACCESS_KEY_ID", iamRoleEnabled)
                  }}
                  component={YBTextInputWithLabel}
                />
              ) : (
                <Field
                  name="AWS_ACCESS_KEY_ID"
                  placeHolder="AWS Access Key"
                  component={YBTextInputWithLabel}
                  validate={required}
                />
              )}
            </Col>
          </Row>
          <Row className="config-provider-row" key={"s3-aws-secret-access-key"}>
            <Col lg={2}>
              <div className="form-item-custom-label">Access Secret</div>
            </Col>
            <Col lg={9}>
            {allowKeyEdits ? (
                <Field
                  name="AWS_SECRET_ACCESS_KEY"
                  placeHolder="AWS Access Secret"
                  input={{
                    value: data["AWS_SECRET_ACCESS_KEY"] || "",
                    disabled: this.disableInputFields(data, "AWS_SECRET_ACCESS_KEY", iamRoleEnabled)
                  }}
                  component={YBTextInputWithLabel}
                />
              ) : (
                <Field
                  name="AWS_SECRET_ACCESS_KEY"
                  placeHolder="AWS Access Secret"
                  component={YBTextInputWithLabel}
                  validate={required}
                />
              )}
            </Col>
          </Row>
          <Row className="config-provider-row" key={"s3-backup-location"}>
            <Col lg={2}>
              <div className="form-item-custom-label">S3 Bucket</div>
            </Col>
            <Col lg={9}>
              {!isEmptyObject(data) ? (
                <Field
                  name="AWS_BACKUP_LOCATION"
                  placeHolder="s3://S3 Bucket"
                  input={{
                    value: data["BACKUP_LOCATION"],
                    disabled: this.disableInputFields(data, "AWS_BACKUP_LOCATION")
                  }}
                  component={YBTextInputWithLabel}
                />
              ) : (
                <Field
                  name="S3_BACKUP_LOCATION"
                  placeHolder="s3://S3 Bucket"
                  component={YBTextInputWithLabel}
                  validate={required}
                />
              )}
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
              {!isEmptyObject(data) ? (
                <Field
                  name="AWS_HOST_BASE"
                  placeHolder="s3.amazonaws.com"
                  input={{
                    value: data["AWS_HOST_BASE"],
                    disabled: this.disableInputFields(data, "AWS_HOST_BASE")
                  }}
                  component={YBTextInputWithLabel}
                />
              ) : (
                <Field
                  name="AWS_HOST_BASE"
                  placeHolder="s3.amazonaws.com"
                  component={YBTextInputWithLabel}
                />
              )}
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
