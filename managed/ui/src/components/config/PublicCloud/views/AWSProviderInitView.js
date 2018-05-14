// Copyright (c) YugaByte, Inc.

import React, { Fragment,  Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel, YBSelectWithLabel, YBToggle } from '../../../common/forms/fields';
import { isDefinedNotNull, isNonEmptyString } from 'utils/ObjectUtils';
import { change, Field } from 'redux-form';
import { IN_DEVELOPMENT_MODE } from '../../../../config';
import { isValidObject, trimString } from '../../../../utils/ObjectUtils';
import {reduxForm} from 'redux-form';

class AWSProviderInitView extends Component {
  constructor(props) {
    super(props);
    this.state = {
      networkSetupType: "new_vpc",
      setupHostedZone: false
    };
  }

  networkSetupChanged = (value) => {
    const { hostInfo } = this.props;
    if (value === "host_vpc") {
      this.updateFormField("destVpcId", hostInfo["aws"]["vpc-id"]);
      this.updateFormField("destVpcRegion", hostInfo["aws"]["region"]);
    } else {
      this.updateFormField("destVpcId", null);
      this.updateFormField("destVpcRegion", null);
    }
    this.setState({networkSetupType: value});
  }

  updateFormField = (field, value) => {
    this.props.dispatch(change("awsProviderConfigForm", field, value));
  };

  createProviderConfig = formValues => {
    const {hostInfo} = this.props;
    const awsProviderConfig = {
      'AWS_ACCESS_KEY_ID': formValues.accessKey,
      'AWS_SECRET_ACCESS_KEY': formValues.secretKey
    };
    if (isDefinedNotNull(formValues.hostedZoneId)) {
      awsProviderConfig['AWS_HOSTED_ZONE_ID'] = formValues.hostedZoneId;
    }
    const regionFormVals = {};
    if (isNonEmptyString(formValues.destVpcId)) {
      regionFormVals["destVpcId"] = formValues.destVpcId;
    }
    if (isNonEmptyString(formValues.destVpcRegion)) {
      regionFormVals["destVpcRegion"] = formValues.destVpcRegion;
    }
    if (this.isHostInAWS()) {
      const awsHostInfo = hostInfo["aws"];
      regionFormVals["hostVpcRegion"] = awsHostInfo["region"];
      regionFormVals["hostVpcId"] = awsHostInfo["vpc-id"];
    }
    this.props.createAWSProvider(formValues.accountName, awsProviderConfig, regionFormVals);
  };

  isHostInAWS = () => {
    const { hostInfo } = this.props;
    return !IN_DEVELOPMENT_MODE && isValidObject(hostInfo) && isValidObject(hostInfo["aws"]) &&
      hostInfo["aws"]["error"] === undefined;
  }

  hostedZoneToggled = (event) => {
    this.setState({setupHostedZone: event.target.checked});
  }

  render() {
    const { handleSubmit, submitting, error} = this.props;
    const network_setup_options = [
      <option key={1} value={"new_vpc"}>{"Create a new VPC"}</option>,
      <option key={2} value={"existing_vpc"}>{"Specify an existing VPC"}</option>
    ];
    if (this.isHostInAWS()) {
      network_setup_options.push(
        <option key={3} value={"host_vpc"}>{"Use VPC of the Admin Console instance"}</option>
      );
    }

    let customVPCFields = <span />;
    if (this.state.networkSetupType !== "new_vpc") {
      customVPCFields = (
        <Fragment>
          <Field name="destVpcRegion" type="text" label="Custom VPC Region"
            component={YBTextInputWithLabel} normalize={trimString}
            isReadOnly={this.state.networkSetupType === "host_vpc"} />
          <Field name="destVpcId" type="text" label="Custom VPC ID"
            component={YBTextInputWithLabel} normalize={trimString}
            isReadOnly={this.state.networkSetupType === "host_vpc"} />
        </Fragment>
      );
    }
    let hostedZoneFields = <span />;
    if (this.state.setupHostedZone) {
      hostedZoneFields = (
        <Field name="hostedZoneId" type="text" label="Route53 Zone ID"
              component={YBTextInputWithLabel} normalize={trimString} />
      );
    }
    return (
      <form name="awsProviderConfigForm" onSubmit={handleSubmit(this.createProviderConfig)}>
        <Row className="config-section-header">
          <Col lg={6}>
            { error && <Alert bsStyle="danger">{error}</Alert> }
            <div className="aws-config-form form-right-aligned-labels">
              <Field name="accountName" type="text" label="Name"
                     component={YBTextInputWithLabel} />
              <Field name="accessKey" type="text" label="Access Key ID"
                     component={YBTextInputWithLabel} normalize={trimString} />
              <Field name="secretKey" type="text" label="Secret Access Key"
                     component={YBTextInputWithLabel} normalize={trimString} />
              <Field name="network_setup" component={YBSelectWithLabel}
                options={network_setup_options} label="VPC Setup"
                onInputChanged={this.networkSetupChanged} />
              {customVPCFields}
              <Field name="setupHostedZone" component={YBToggle}
                     label="Enable Hosted Zone" defaultChecked={this.state.setupHostedZone}
                     onToggle={this.hostedZoneToggled} />
              {hostedZoneFields}
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
  if (!isNonEmptyString(values.accountName)) {
    errors.accountName = 'Account Name is required';
  }

  if (!values.accessKey || values.accessKey.trim() === '') {
    errors.accessKey = 'Access Key is required';
  }

  if(!values.secretKey || values.secretKey.trim() === '') {
    errors.secretKey = 'Secret Key is required';
  }

  if (values.network_setup === "existing_vpc") {
    if (!isNonEmptyString(values.destVpcId)) {
      errors.destVpcId = 'VPC ID is required';
    }
    if (!isNonEmptyString(values.destVpcRegion)) {
      errors.destVpcRegion = 'VPC region is required';
    }
  }

  if (values.setupHostedZone && !isNonEmptyString(values.hostedZoneId)) {
    errors.hostedZoneId = 'Route53 Zone ID is required';
  }
  return errors;
}

export default reduxForm({
  form: 'awsProviderConfigForm',
  validate
})(AWSProviderInitView);
