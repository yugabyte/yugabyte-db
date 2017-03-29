// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel } from '../../common/forms/fields';
import ProviderConfiguration from '../ConfigProvider/ProviderConfiguration';
import { ProgressList } from '../../common/indicators';
import { DescriptionList } from '../../common/descriptors';
import { Field } from 'redux-form';
import  { isValidArray, isValidObject } from '../../../utils/ObjectUtils';
import { RegionMap } from '../../maps';

const PROVIDER_TYPE = "aws";

export default class AWSProviderConfiguration extends ProviderConfiguration {
  componentWillMount() {
    this.props.getProviderListItems();
    this.props.getSupportedRegionList();
  }
  componentWillUnmount() {
    this.props.resetProviderBootstrap();
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
    this.props.createProvider(PROVIDER_TYPE, formValues.accountName, awsProviderConfig);
  }
  deleteProviderConfig(provider) {
    this.props.deleteProviderConfig(provider.uuid);
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
      if (type !== "initialize" && type !== "cleanup") {
        // Set the state for next step until initialize step (last one).
        bootstrapSteps[currentStepIndex + 1].state = "Running";
      }

      switch (type) {
        case "provider":
          this.setState({providerUUID: response.uuid, accountName: response.name})
          // TODO: fetch the yugaware region code from api.
          this.props.createRegion(response.uuid, "us-west-2");
          break;
        case "region":
          var accessKeyCode = "yb-" + this.state.accountName.toLowerCase() + "-key"
          this.props.createAccessKey(this.state.providerUUID, response.uuid, accessKeyCode);
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
        case "cleanup":
          if (this.props.cloudBootstrap !== nextProps.cloudBootstrap) {
            this.props.getProviderListItems();
          }
          break;
        default:
          break;
      }
    }
  }

  render() {
    const { handleSubmit, submitting, pristine, reset,
      cloudBootstrap: { loading, type, error },
      configuredProviders, configuredRegions, accessKeys } = this.props;

    var awsProvider = configuredProviders.find((provider) => provider.code === PROVIDER_TYPE)
    var providerConfig;
    if (isValidObject(awsProvider)) {
      var awsRegions = configuredRegions.filter(
        (configuredRegion) => configuredRegion.provider.code === PROVIDER_TYPE
      );
      var regionList = "Not Configured"
      var zoneList = "Not Configured"
      var accessKeyList = "Not Configured"

      if (isValidArray(awsRegions)) {
        regionList = awsRegions.map( (awsRegion) => awsRegion.name ).join(",")
        zoneList = awsRegions.map( (awsRegion) => awsRegion.zones.map( (zone) => zone.name )).join(",")
      }
      if (isValidObject(accessKeys) && isValidArray(accessKeys.data)) {
        accessKeyList = accessKeys.data.map( (accessKey) => accessKey.idKey.keyCode ).join(",")
      }
      var providerInfo = [
        {name: "Provider", data: awsProvider.name },
        {name: "Regions", data: regionList },
        {name: "Zones", data: zoneList },
        {name: "Access Keys", data: accessKeyList}
      ]

      providerConfig =
        <form name="awsConfigForm" onSubmit={handleSubmit(this.deleteProviderConfig.bind(this, awsProvider))}>
          <Row className="config-section-header">
            <h4>AWS configuration</h4>
            <DescriptionList listItems={providerInfo} />
            <RegionMap title="All Supported Regions" regions={awsRegions} type="Root"/>
          </Row>
          <Row className="form-action-button-container">
            <Col lg={4} lgOffset={8}>
              <YBButton btnText={"Delete"} btnClass={"btn btn-default delete-btn"}
                        disabled={submitting}  btnType="submit"/>
              <YBButton btnText={"Cancel"} btnClass={"btn btn-default cancel-btn"}
                        disabled={pristine || submitting} onClick={reset} />
            </Col>
          </Row>
        </form>
    } else {
      var bootstrapSteps = <span />;
      // We don't have bootstrap steps for cleanup.
      if (type && type !== "cleanup") {
        var progressDetailsMap = this.state.bootstrapSteps.map( (step) => {
          return { name: step.name, type: step.state }
        })
        bootstrapSteps =
          <div className="aws-config-progress">
            <h5>Bootstrap Steps:</h5>
            <Col lg={12}>
              <ProgressList items={progressDetailsMap} />
            </Col>
          </div>
      }
      providerConfig =
        <form name="awsConfigForm" onSubmit={handleSubmit(this.createProviderConfig.bind(this))}>
          <Row className="config-section-header">
            <Col lg={12}>
              { error && <Alert bsStyle="danger">{error}</Alert> }
              <div className="aws-config-form">
                <Field name="accountName" type="text" component={YBTextInputWithLabel} label="Account Name" />
                <Field name="accessKey" type="text" component={YBTextInputWithLabel} label="Access Key"/>
                <Field name="secretKey" type="text" component={YBTextInputWithLabel} label="Secret Key"/>
              </div>
              { bootstrapSteps }
            </Col>
          </Row>
          <Row className="form-action-button-container">
            <Col lg={4} lgOffset={8}>
              <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}
                        disabled={submitting || loading } btnType="submit"/>
              <YBButton btnText={"Cancel"} btnClass={"btn btn-default cancel-btn"}
                        disabled={pristine || submitting} onClick={reset} />
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
