// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton, YBInputField } from '../../common/forms/fields';
import ProviderConfiguration from '../ConfigProvider/ProviderConfiguration';
import { ProgressList } from '../../common/indicators';
import { DescriptionList } from '../../common/descriptors';
import { Field } from 'redux-form';
import  { isValidArray, isValidObject } from '../../../utils/ObjectUtils';
import { RegionMap } from '../../maps';

const PROVIDER_TYPE = "aws";
const ACCESS_KEY_CODE = "yb-access-key";

export default class AWSProviderConfiguration extends ProviderConfiguration {
  componentWillMount() {
    this.props.getSupportedRegionList();
  }

  getInitialState() {
      return {
        bootstrapSteps: [
          {type: "provider", name: "Create Provider", state: "Initializing"},
          {type: "region", name: "Create Region and Zones", state: "Initializing"},
          {type: "access-key", name: "Create Access Key", state: "Initializing"},
          {type: "initialize", name: "Create Instance Types", state: "Initializing"}
        ]
      };
  }
  createProviderConfig(formValues) {
    const awsProviderConfig = {
      'AWS_ACCESS_KEY_ID': formValues.accessKey,
      'AWS_SECRET_ACCESS_KEY': formValues.secretKey
    }
    this.props.createProvider(PROVIDER_TYPE, awsProviderConfig);
  }
  componentWillReceiveProps(nextProps) {
    const { cloudBootstrap: { loading, response, error, type } } = nextProps;
    const { bootstrapSteps } = this.state;
    var currentStepIndex = bootstrapSteps.findIndex( (step) => step.type === type );
    if (currentStepIndex !== -1) {
      if (loading) {
        bootstrapSteps[currentStepIndex].state = "Running"
      } else {
        bootstrapSteps[currentStepIndex].state = error ? "Error" : "Success";
      }
      this.setState({bootstrapSteps: bootstrapSteps});
    }
    if (isValidObject(response)) {
      if (type !== "initialize") {
        // Set the state for next step until initialize step (last one).
        bootstrapSteps[currentStepIndex + 1].state = "Running";
      }

      switch (type) {
        case "provider":
          this.setState({providerUUID: response.uuid})
          // TODO: fetch the yugaware region code from api.
          this.props.createRegion(response.uuid, "us-west-2");
          break;
        case "region":
          this.props.createAccessKey(this.state.providerUUID, response.uuid, ACCESS_KEY_CODE);
          this.setState({regionUUID: response.uuid})
          break;
        case "access-key":
          // TODO: change this, currently AWS initializer seems to be blocking get api.
          this.setState({bootstrapSteps: bootstrapSteps});
          this.props.initializeProvider(this.state.providerUUID);
          break;
        case "initialize":
          if (this.props.cloudBootstrap !== nextProps.cloudBootstrap) {
            // This would refresh the data once the initialize is done.
            this.props.getProviderListItems();
            this.props.getSupportedRegionList();
          }
          break;
        default:
          break;
      }
    }
  }

  render() {
    const { handleSubmit, cloudBootstrap: { type, error }, configuredRegions } = this.props;
    var awsRegions = configuredRegions.filter(
      (configuredRegion) => configuredRegion.provider.code === PROVIDER_TYPE
    );

    var providerConfig;
    if (isValidArray(awsRegions)) {
      var regionList = awsRegions.map( (awsRegion) => awsRegion.name ).join(",")
      var zoneList = awsRegions.map( (awsRegion) => awsRegion.zones.map( (zone) => zone.name )).join(",")
      var providerInfo = [
        {name: "Provider", data: awsRegions[0].provider.name },
        {name: "Regions", data: regionList },
        {name: "Zones", data: zoneList }
      ]
      providerConfig =
        <Row className="config-section-header">
          <h4>AWS configuration</h4>
          <DescriptionList listItems={providerInfo} />
          <RegionMap title="All Supported Regions" regions={awsRegions} type="Root"/>
        </Row>;
    } else {
      var progressDetailsMap = this.state.bootstrapSteps.map( (step) => {
        return { name: step.name, type: step.state }
      })
      providerConfig =
        <form name="awsConfigForm" onSubmit={handleSubmit(this.createProviderConfig.bind(this))}>
          <Row className="config-section-header">
            <Col lg={12}>
              { error && <Alert bsStyle="danger">{error}</Alert> }
              <div className="aws-config-form">
                <Field name="accessKey" type="text" component={YBInputField} label="Access Key"/>
                <Field name="secretKey" type="text" component={YBInputField} label="Secret Key"/>
              </div>
              { type &&
                <div className="aws-config-progress">
                  <h5>Bootstrap Steps:</h5>
                  <Col lg={12}>
                    <ProgressList items={progressDetailsMap} />
                  </Col>
                </div>
              }
            </Col>
          </Row>
          <Row className="form-action-button-container">
            <Col lg={4} lgOffset={8}>
              <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"} btnType="submit"/>
              <YBButton btnText={"Cancel"} btnClass={"btn btn-default cancel-btn"}/>
            </Col>
          </Row>
        </form>;
    }

    return (
      <div className="provider-config-container">
        { providerConfig }
      </div>
    )
  }
}
