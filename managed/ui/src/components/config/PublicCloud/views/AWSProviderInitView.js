// Copyright (c) YugaByte, Inc.

import React, { Fragment,  Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel, YBSelectWithLabel, YBToggle } from '../../../common/forms/fields';
import { isDefinedNotNull, isNonEmptyString } from 'utils/ObjectUtils';
import { change, Field } from 'redux-form';
import { isValidObject, trimString } from '../../../../utils/ObjectUtils';
import {reduxForm} from 'redux-form';

class AWSProviderInitView extends Component {
  constructor(props) {
    super(props);
    this.state = {
      networkSetupType: "new_vpc",
      setupHostedZone: false,
      credentialInputType: "custom_keys"
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
      this.updateFormField("azToSubnetIds", null);
      this.updateFormField("customSecurityGroupId", null);
    }
    if (value !== "new_vpc") {
      this.updateFormField("regionList", null);
    }
    this.setState({networkSetupType: value});
  }

  credentialInputChanged = (value) => {
    this.setState({credentialInputType: value});
  }

  updateFormField = (field, value) => {
    this.props.dispatch(change("awsProviderConfigForm", field, value));
  };

  createProviderConfig = formValues => {
    const {hostInfo} = this.props;
    const awsProviderConfig = {};
    if (formValues.credential_input === "custom_keys") {
      awsProviderConfig['AWS_ACCESS_KEY_ID'] = formValues.accessKey;
      awsProviderConfig['AWS_SECRET_ACCESS_KEY'] = formValues.secretKey;
    };
    if (isDefinedNotNull(formValues.hostedZoneId)) {
      awsProviderConfig['AWS_HOSTED_ZONE_ID'] = formValues.hostedZoneId;
    }
    const regionFormVals = {};
    if (this.isHostInAWS()) {
      const awsHostInfo = hostInfo["aws"];
      regionFormVals["hostVpcRegion"] = awsHostInfo["region"];
      regionFormVals["hostVpcId"] = awsHostInfo["vpc-id"];
    }
    const perRegionMetadata = {};
    if (formValues.network_setup !== "new_vpc") {
      const azMapping = {};
      const mappings = formValues.azToSubnetIds.split(",");
      for (const idx in mappings) {
        const mapping = mappings[idx];
        const pieces = mapping.split(":");
        // TODO(bogdan): this is temporary so ignore validation...
        azMapping[pieces[0]] = pieces[1];
      }
      perRegionMetadata[formValues.destVpcRegion] = {
        "vpcId": formValues.destVpcId,
        "azToSubnetIds": azMapping,
        "customSecurityGroupId": formValues.customSecurityGroupId
      };

      regionFormVals["perRegionMetadata"] = perRegionMetadata;
    } else {
      const regions = formValues.regionList.split(",");
      for (const idx in regions) {
        perRegionMetadata[regions[idx]] = {};
      }
      regionFormVals["perRegionMetadata"] = perRegionMetadata;
    }
    this.props.createAWSProvider(formValues.accountName, awsProviderConfig, regionFormVals);
  };

  isHostInAWS = () => {
    const { hostInfo } = this.props;
    return isValidObject(hostInfo) && isValidObject(hostInfo["aws"]) &&
      hostInfo["aws"]["error"] === undefined;
  }

  hostedZoneToggled = (event) => {
    this.setState({setupHostedZone: event.target.checked});
  }

  generateRow = (label, field) => {
    return (
      <Row className="config-provider-row">
        <Col lg={3}>
          <div className="form-item-custom-label">
            {label}
          </div>
        </Col>
        <Col lg={7}>
          {field}
        </Col>
      </Row>
    );
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
      const destVpcRegionField = (
        <Field name="destVpcRegion" type="text" component={YBTextInputWithLabel}
          normalize={trimString} isReadOnly={this.state.networkSetupType === "host_vpc"} />
      );
      const destVpcIdField = (
        <Field name="destVpcId" type="text" component={YBTextInputWithLabel}
          normalize={trimString} isReadOnly={this.state.networkSetupType === "host_vpc"} />
      );
      const azToSubnetIdsField = (
        <Field name="azToSubnetIds" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />
      );
      const customSecurityGroupIdField = (
        <Field name="customSecurityGroupId" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />
      );
      customVPCFields = (
        <Fragment>
          {this.generateRow("Custom VPC Region", destVpcRegionField)}
          {this.generateRow("Custom VPC ID", destVpcIdField)}
          {this.generateRow("AZ to Subnet CSV", azToSubnetIdsField)}
          {this.generateRow("Security Group ID", customSecurityGroupIdField)}
        </Fragment>
      );
    } else {
      const regionListField = (
        <Field name="regionList" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />
      );
      customVPCFields = (
        <Fragment>
          {this.generateRow("Region List CSV", regionListField)}
        </Fragment>
      );
    }
    let hostedZoneField = <span />;
    if (this.state.setupHostedZone) {
      hostedZoneField = this.generateRow("Route 53 Zone ID",
        <Field name="hostedZoneId" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />
      );
    }
    const credential_input_options = [
      <option key={1} value={"custom_keys"}>{"Input Access and Secret keys"}</option>,
      <option key={2} value={"local_iam_role"}>{"Use IAM Role on instance"}</option>
    ];
    let customKeyFields = <span />;
    if (this.state.credentialInputType === "custom_keys") {
      const accessKeyField = (
        <Field name="accessKey" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />);
      const secretKeyField = (
        <Field name="secretKey" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />);
      customKeyFields = (
        <Fragment>
          {this.generateRow("Access Key ID", accessKeyField)}
          {this.generateRow("Secret Access Key", secretKeyField)}
        </Fragment>
      );
    }
    const nameField = this.generateRow(
      "Name",
      <Field name="accountName" type="text" component={YBTextInputWithLabel} />);
    const credentialInputField = this.generateRow(
      "Credential Type",
      <Field name="credential_input" component={YBSelectWithLabel}
        options={credential_input_options} onInputChanged={this.credentialInputChanged} />
    );
    const networkInputField = this.generateRow(
      "VPC Setup",
      <Field name="network_setup" component={YBSelectWithLabel} options={network_setup_options}
        onInputChanged={this.networkSetupChanged} />);
    const hostedZoneToggleField = this.generateRow(
      "Enable Hosted Zone",
      <Field name="setupHostedZone" component={YBToggle}
        defaultChecked={this.state.setupHostedZone} onToggle={this.hostedZoneToggled} />);
    return (
      <div className="provider-config-container">
        <form name="awsProviderConfigForm" onSubmit={handleSubmit(this.createProviderConfig)}>
          <div className="editor-container">
            <Row className="config-section-header">
              <Col lg={8}>
                {error && <Alert bsStyle="danger">{error}</Alert>}
                {nameField}
                {credentialInputField}
                {customKeyFields}
                {networkInputField}
                {customVPCFields}
                {hostedZoneToggleField}
                {hostedZoneField}
              </Col>
            </Row>
          </div>
          <div className="form-action-button-container">
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}
                      disabled={submitting } btnType="submit"/>
          </div>
        </form>
      </div>
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
  if (values.network_setup === "existing_vpc" || values.network_setup === "host_vpc") {
    if (!isNonEmptyString(values.azToSubnetIds)) {
      errors.azToSubnetIds = 'AZ to Subnet mapping is required';
    }
    if (!isNonEmptyString(values.customSecurityGroupId)) {
      errors.customSecurityGroupId = 'Security Group ID is required';
    }
  }
  if (values.network_setup === "new_vpc") {
    if (!isNonEmptyString(values.regionList)) {
      errors.regionList = 'Region List must not be empty';
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
