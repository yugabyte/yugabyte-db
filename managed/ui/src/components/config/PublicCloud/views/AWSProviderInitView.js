// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel, YBToggle } from '../../../common/forms/fields';
import { isDefinedNotNull } from 'utils/ObjectUtils';
import { Field } from 'redux-form';
import { IN_DEVELOPMENT_MODE } from '../../../../config';
import { isValidObject, trimString, convertSpaceToDash } from '../../../../utils/ObjectUtils';
import {reduxForm} from 'redux-form';

const PROVIDER_TYPE = "aws";

class AWSProviderInitView extends Component {
  constructor(props) {
    super(props);
    this.createProviderConfig = this.createProviderConfig.bind(this);
    this.isHostInAWS = this.isHostInAWS.bind(this);
    this.state = {useHostVpc: false};
  }

  createProviderConfig(formValues) {
    const {hostInfo} = this.props;
    this.setState({useHostVpc: isDefinedNotNull(formValues.useHostVpc)});
    const awsProviderConfig = {
      'AWS_ACCESS_KEY_ID': formValues.accessKey,
      'AWS_SECRET_ACCESS_KEY': formValues.secretKey
    };
    let regionFormVals = {};
    if (this.isHostInAWS()) {
      regionFormVals = {
        "regionList": [hostInfo["region"]],
        "hostVpcId": hostInfo["vpc-id"],
        "destVpcId": this.state.useHostVpc ? hostInfo["vpc-id"] : "",
      };
    } else {
      // TODO: Temporary change to it work locally.
      regionFormVals = {"regionList": ["us-west-2"], "hostVpcId": ""};
    }
    this.props.createAWSProvider(PROVIDER_TYPE, formValues.accountName, awsProviderConfig, regionFormVals);
  }

  isHostInAWS() {
    const { hostInfo } = this.props;
    return !IN_DEVELOPMENT_MODE && (isValidObject(hostInfo) && hostInfo["error"] === undefined);
  }

  render() {
    const { handleSubmit, submitting, error} = this.props;
    const subLabel = "Disabled if host is not on AWS";
    return (
      <form name="providerConfigForm" onSubmit={handleSubmit(this.createProviderConfig)}>
        <Row className="config-section-header">
          <Col lg={6}>
            <h4>AWS Account Info</h4>
            { error && <Alert bsStyle="danger">{error}</Alert> }
            <div className="aws-config-form form-right-aligned-labels">
              <Field name="accountName" type="text" label="Name"
                     component={YBTextInputWithLabel} normalize={convertSpaceToDash} />
              <Field name="accessKey" type="text" label="Access Key ID"
                     component={YBTextInputWithLabel} normalize={trimString} />
              <Field name="secretKey" type="text" label="Secret Access Key"
                     component={YBTextInputWithLabel} normalize={trimString} />
              <Field name="useHostVpc"
                     component={YBToggle}
                     label="Use Host's VPC"
                     subLabel={subLabel}
                     defaultChecked={this.state.useHostVpc}
                     isReadOnly={!this.isHostInAWS()} />
            </div>
          </Col>
        </Row>
        <div className="form-action-button-container">
          <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}
                    disabled={submitting } btnType="submit"/>
        </div>
      </form>
    );
  }
}

function validate(values) {
  const errors = {};
  let hasErrors = false;
  if (!values.accountName) {
    errors.accountName = 'Name is required';
    hasErrors = true;
  }

  if (/\s/.test(values.accountName)) {
    errors.accountName = 'Name cannot have spaces';
    hasErrors = true;
  }

  if (!values.accessKey || values.accessKey.trim() === '') {
    errors.accessKey = 'Access Key is required';
    hasErrors = true;
  }

  if(!values.secretKey || values.secretKey.trim() === '') {
    errors.secretKey = 'Secret Key is required';
    hasErrors = true;
  }
  return hasErrors && errors;
}

export default reduxForm({
  form: 'providerConfigForm',
  validate
})(AWSProviderInitView);
