// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel, YBToggle } from '../../../common/forms/fields';
import { isDefinedNotNull } from 'utils/ObjectUtils';
import { Field } from 'redux-form';
import { IN_DEVELOPMENT_MODE } from '../../../../config';
import { isValidObject, trimString, convertSpaceToDash } from '../../../../utils/ObjectUtils';
import {reduxForm} from 'redux-form';

class AWSProviderInitView extends Component {
  constructor(props) {
    super(props);
    this.state = {
      hostVpcVisible: true,
    };
    this.hostVpcToggled = this.hostVpcToggled.bind(this);
  }

  createProviderConfig = formValues => {
    const {hostInfo} = this.props;
    const awsProviderConfig = {
      'AWS_ACCESS_KEY_ID': formValues.accessKey,
      'AWS_SECRET_ACCESS_KEY': formValues.secretKey
    };
    if (isDefinedNotNull(formValues.hostedZoneId)) {
      awsProviderConfig['AWS_HOSTED_ZONE_ID'] = formValues.hostedZoneId;
    }
    let regionFormVals = {};

    if (this.isHostInAWS()) {
      const awsHostInfo = hostInfo["aws"];
      regionFormVals = {
        "regionList": [],
        "hostedZoneId": formValues.hostedZoneId,
        "hostVpcRegion": awsHostInfo["region"],
        "hostVpcId": awsHostInfo["vpc-id"],
        "destVpcId": isDefinedNotNull(formValues.useHostVpc) ? awsHostInfo["vpc-id"] : "",
      };
    } else {
      regionFormVals = {
        "regionList": [],
        "hostedZoneId": formValues.hostedZoneId,
        // TODO: this should really be called destVpcRegion, but we're piggybacking on this through
        // YW and devops.
        // DEFAULT FOR PORTAL OR LOCAL: us-west-2
        "hostVpcRegion": formValues.destVpcRegion,
        // DEFAULT FOR PORTAL OR LOCAL: vpc-0fe36f6b
        "destVpcId": formValues.destVpcId
      };
    }
    this.props.createAWSProvider(formValues.accountName, awsProviderConfig, regionFormVals);
  };

  isHostInAWS = () => {
    const { hostInfo } = this.props;
    return !IN_DEVELOPMENT_MODE && isValidObject(hostInfo) && isValidObject(hostInfo["aws"]) &&
      hostInfo["aws"]["error"] === undefined;
  }

  hostVpcToggled(event) {
    this.setState({hostVpcVisible: !event.target.checked});
  }

  render() {
    const { handleSubmit, submitting, error} = this.props;
    const subLabel = "Disabled if host is not on AWS";
    let hostVpcRegionField = <span />;
    let hostVpcIdField = <span />;
    if (this.state.hostVpcVisible) {
      hostVpcRegionField = (
        <Field name="destVpcRegion" type="text" label="Custom VPC Region"
               component={YBTextInputWithLabel} normalize={trimString} />
      );
      hostVpcIdField = (
        <Field name="destVpcId" type="text" label="Custom VPC ID"
               component={YBTextInputWithLabel} normalize={trimString} />
      );
    }
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
              <Field name="hostedZoneId" type="text" label="Route53 Hosted Zone ID"
                     component={YBTextInputWithLabel} normalize={trimString} />
              {hostVpcRegionField}
              {hostVpcIdField}
              <Field name="useHostVpc"
                     component={YBToggle}
                     label="Use Host's VPC"
                     subLabel={subLabel}
                     defaultChecked={!this.state.hostVpcVisible}
                     onToggle={this.hostVpcToggled}
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
