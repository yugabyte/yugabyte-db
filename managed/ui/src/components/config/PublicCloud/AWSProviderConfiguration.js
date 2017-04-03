// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel } from '../../common/forms/fields';
import { ProgressList } from '../../common/indicators';
import { DescriptionList } from '../../common/descriptors';
import { YBConfirmModal } from '../../modals';
import { Field } from 'redux-form';
import {withRouter} from 'react-router';
import  { isValidArray, isValidObject } from '../../../utils/ObjectUtils';
import { RegionMap } from '../../maps';

const PROVIDER_TYPE = "aws";

class AWSProviderConfiguration extends Component {
  constructor(props) {
    super(props);
    this.state = {
      bootstrapSteps: [
        {type: "provider", name: "Create Provider", state: "Initializing"},
        {type: "region", name: "Create Region and Zones", state: "Initializing"},
        {type: "access-key", name: "Create Access Key", state: "Initializing"},
        {type: "initialize", name: "Create Instance Types", state: "Initializing"}
      ]
    }
  }
  componentWillMount() {
    this.props.getProviderListItems();
    this.props.getSupportedRegionList();
    // TODO: may be call this only once if not already fetched?
    this.props.fetchHostInfo();
  }
  componentWillUnmount() {
    this.props.resetProviderBootstrap();
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
    const { cloudBootstrap: { loading, response, error, type }} = nextProps;
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
          const { hostInfo } = this.props;
          if (isValidObject(hostInfo) && hostInfo["error"] === undefined) {
            this.props.createRegion(response.uuid, hostInfo["region"], hostInfo["vpc-id"]);
          } else {
            // TODO: Temporary change to it work locally.
            this.props.createRegion(response.uuid, "us-west-2", "");
          }
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
    const { handleSubmit, submitting, cloudBootstrap: { loading, type, error },
      configuredProviders, configuredRegions, accessKeys, universeList } = this.props;
    var universeExistsForProvider = false;
    if (isValidArray(configuredProviders) && isValidArray(universeList)){
      universeExistsForProvider = universeList.some(universe => universe.provider.uuid === configuredProviders[0].uuid);
    }

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
        {name: "Account Name", data: awsProvider.name },
        {name: "Key Pair", data: accessKeyList},
        {name: "Regions", data: regionList },
        {name: "Zones", data: zoneList },
      ]

      providerConfig =
        <div>
          <Row className="config-section-header">
            <Col md={12}>
              <YBConfirmModal name="delete-aws-provider" title={"Confirm Delete"}
                btnLabel="Delete Configuration" btnClass="btn btn-default delete-btn delete-aws-btn" disabled={universeExistsForProvider}
                onConfirm={handleSubmit(this.deleteProviderConfig.bind(this, awsProvider))}>
                Are you sure you want to delete this AWS configuration?
              </YBConfirmModal>
              <DescriptionList listItems={providerInfo} />
              <RegionMap title="All Supported Regions" regions={awsRegions} type="Root"/>
            </Col>
          </Row>
        </div>
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
              <h4>AWS Account Info</h4>
              { error && <Alert bsStyle="danger">{error}</Alert> }
              <div className="aws-config-form form-right-aligned-labels">
                <Field name="accountName" type="text" component={YBTextInputWithLabel} label="Account Name" />
                <Field name="accessKey" type="text" component={YBTextInputWithLabel} label="Access Key ID"/>
                <Field name="secretKey" type="text" component={YBTextInputWithLabel} label="Secret Access Key"/>
              </div>
              { bootstrapSteps }
            </Col>
          </Row>
          <div className="form-action-button-container">
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}
                      disabled={submitting || loading } btnType="submit"/>
          </div>
        </form>;
    }

    return (
      <div className="provider-config-container">
        { providerConfig }
      </div>
    );
  }
}

export default withRouter(AWSProviderConfiguration);
