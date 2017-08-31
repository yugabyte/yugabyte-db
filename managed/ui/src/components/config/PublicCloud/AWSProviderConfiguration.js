// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel, YBToggle } from '../../common/forms/fields';
import { getPromiseState } from 'utils/PromiseUtils';
import { isDefinedNotNull } from 'utils/ObjectUtils';
import { ProgressList } from '../../common/indicators';
import { DescriptionList } from '../../common/descriptors';
import { YBConfirmModal } from '../../modals';
import { Field } from 'redux-form';
import { withRouter } from 'react-router';
import { IN_DEVELOPMENT_MODE } from '../../../config';

import { isValidObject, trimString, convertSpaceToDash, isNonEmptyArray }
from '../../../utils/ObjectUtils';
import { RegionMap, YBMapLegend } from '../../maps';

const PROVIDER_TYPE = "aws";

class AWSProviderConfiguration extends Component {
  constructor(props) {
    super(props);
    const { cloudBootstrap: {data: { type }, promiseState}} = props;
    this.state = {
      bootstrapSteps: [
        {type: "provider", name: "Create Provider", state: "Initializing"},
        {type: "region", name: "Create Region and Zones", state: "Initializing"},
        {type: "accessKey", name: "Create Access Key", state: "Initializing"},
        {type: "initialize", name: "Create Instance Types", state: "Initializing"}
      ],
      useHostVpc: false,
      refreshing: type === "initialize" && promiseState.isLoading()
    };
    this.createProviderConfig = this.createProviderConfig.bind(this);
    this.isHostInAWS = this.isHostInAWS.bind(this);
  }

  componentWillUnmount() {
    // this.props.resetProviderBootstrap();
  }

  createProviderConfig(formValues) {
    this.setState({useHostVpc: isDefinedNotNull(formValues.useHostVpc)});
    const awsProviderConfig = {
      'AWS_ACCESS_KEY_ID': formValues.accessKey,
      'AWS_SECRET_ACCESS_KEY': formValues.secretKey
    };
    this.props.createProvider(PROVIDER_TYPE, formValues.accountName, awsProviderConfig);
  }

  deleteProviderConfig(provider) {
    this.props.deleteProviderConfig(provider.uuid);
  }

  isHostInAWS() {
    const { hostInfo } = this.props;
    return !IN_DEVELOPMENT_MODE && (isValidObject(hostInfo) && hostInfo["error"] === undefined);
  }

  refreshPricingData(provider) {
    this.props.initializeProvider(provider.uuid);
    this.setState({refreshing: true});
  }

  componentWillReceiveProps(nextProps) {
    const { cloudBootstrap: {data: { response, type }, error, promiseState}} = nextProps;
    const { bootstrapSteps, refreshing } = this.state;

    if (refreshing && type === "initialize" && !promiseState.isLoading()) {
      this.setState({refreshing: false});
    }

    const currentStepIndex = bootstrapSteps.findIndex( (step) => step.type === type );
    if (currentStepIndex !== -1) {
      if (promiseState.isLoading()) {
        bootstrapSteps[currentStepIndex].state = "Running";
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
          this.setState({providerUUID: response.uuid, accountName: response.name});
          const { hostInfo } = this.props;
          let regionFormVals = {};
          if (this.isHostInAWS()) {
            regionFormVals = {
              "code": hostInfo["region"],
              "hostVpcId": hostInfo["vpc-id"],
              "destVpcId": this.state.useHostVpc ? hostInfo["vpc-id"] : "",
              "name": hostInfo["region"]
            };
          } else {
            // TODO: Temporary change to it work locally.
            regionFormVals = {"code": "us-west-2", "name": "us-west-2"};
          }
          this.props.createRegion(response.uuid, regionFormVals);
          break;
        case "region":
          const accessKeyCode = "yb-" + this.state.accountName.toLowerCase() + "-key";
          this.props.createAccessKey(this.state.providerUUID, response.uuid, accessKeyCode);
          this.setState({regionUUID: response.uuid});
          break;
        case "accessKey":
          // TODO: change this, currently AWS initializer seems to be blocking get api.
          this.setState({bootstrapSteps: bootstrapSteps});
          this.props.initializeProvider(this.state.providerUUID);
          break;
        case "initialize":
          if (this.props.cloudBootstrap !== nextProps.cloudBootstrap) {
            this.props.reloadCloudMetadata();
          }
          break;
        case "cleanup":
          if (this.props.cloudBootstrap !== nextProps.cloudBootstrap) {
            this.props.reloadCloudMetadata();
          }
          break;
        default:
          break;
      }
    }
  }

  render() {
    const { handleSubmit, submitting, cloudBootstrap: { data: { type }, promiseState, error },
      configuredProviders, configuredRegions, accessKeys, universeList, hostInfo } = this.props;
    const { refreshing } = this.state;
    const awsProvider = configuredProviders.data.find((provider) => provider.code === PROVIDER_TYPE);
    let universeExistsForProvider = false;
    let providerConfig;
    if (isValidObject(awsProvider)) {
      if (getPromiseState(configuredProviders).isSuccess() && getPromiseState(universeList).isSuccess()){
        universeExistsForProvider = universeList.data.some(universe => universe.provider && (universe.provider.uuid === awsProvider.uuid));
      }
      const awsRegions = configuredRegions.data.filter(
        (configuredRegion) => configuredRegion.provider.code === PROVIDER_TYPE
      );

      let keyPairName = "Not Configured";
      if (isValidObject(accessKeys) && isNonEmptyArray(accessKeys.data)) {
        const awsAccessKey = accessKeys.data.find((accessKey) => accessKey.idKey.providerUUID === awsProvider.uuid);
        if (isDefinedNotNull(awsAccessKey)) {
          keyPairName = awsAccessKey.idKey.keyCode;
        }
      }

      const providerInfo = [
        {name: "Name", data: awsProvider.name },
        {name: "SSH Key", data: keyPairName},
      ];

      const deleteButtonDisabled = refreshing || submitting || universeExistsForProvider;
      const buttonBaseClassName = "btn btn-default manage-provider-btn";
      let deleteButtonClassName = buttonBaseClassName;
      let deleteButtonTitle = "Delete this AWS configuration.";
      if (deleteButtonDisabled) {
        deleteButtonTitle = "Can't delete this AWS configuration because one or more AWS universes are currently defined. Delete all AWS universes first.";
      } else {
        deleteButtonClassName += " delete-btn";
      }

      let refreshPricingLabel = "Refresh Pricing Data";
      if (refreshing) {
        refreshPricingLabel = "Refreshing...";
      }

      providerConfig = (
        <div>
          <Row className="config-section-header">
            <Col md={12}>
              <span className="pull-right" title={deleteButtonTitle}>
                <YBButton btnText="Delete Configuration" disabled={deleteButtonDisabled}
                          btnClass={deleteButtonClassName} onClick={this.props.showDeleteProviderModal}/>
                <YBButton btnText={refreshPricingLabel} btnClass={buttonBaseClassName} disabled={refreshing}
                          onClick={this.refreshPricingData.bind(this, awsProvider)} />
                <YBConfirmModal name="delete-aws-provider" title={"Confirm Delete"}
                                onConfirm={handleSubmit(this.deleteProviderConfig.bind(this, awsProvider))}
                                currentModal="deleteAWSProvider" visibleModal={this.props.visibleModal}
                                hideConfirmModal={this.props.hideDeleteProviderModal}>
                  Are you sure you want to delete this AWS configuration?
                </YBConfirmModal>
              </span>
              <DescriptionList listItems={providerInfo} />
            </Col>
          </Row>
          <Row>
            <Col lg={12} className="provider-map-container">
              <RegionMap title="All Supported Regions" regions={awsRegions} type="Region" showLabels={true} showRegionLabels={true} />
              <YBMapLegend title="Region Map" />
            </Col>
          </Row>
        </div>
      );
    } else {
      let bootstrapSteps = <span />;
      // We don't have bootstrap steps for cleanup.
      if (type && type !== "cleanup") {
        const progressDetailsMap = this.state.bootstrapSteps.map( (step) => {
          return { name: step.name, type: step.state };
        });
        bootstrapSteps = (
          <div className="aws-config-progress">
            <h5>Bootstrap Steps:</h5>
            <Col lg={12}>
              <ProgressList items={progressDetailsMap} />
            </Col>
          </div>
        );
      }

      let subLabel = "Disabled if host is not on AWS";
      if (this.isHostInAWS()) {
        subLabel = " (" + hostInfo["vpc-id"] + ")";
      }

      providerConfig = (
        <form name="awsConfigForm" onSubmit={handleSubmit(this.createProviderConfig)}>
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
              { bootstrapSteps }
            </Col>
          </Row>
          <div className="form-action-button-container">
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}
                      disabled={submitting || promiseState.isLoading() } btnType="submit"/>
          </div>
        </form>
      );
    }

    return (
      <div className="provider-config-container">
        { providerConfig }
      </div>
    );
  }
}

export default withRouter(AWSProviderConfiguration);
