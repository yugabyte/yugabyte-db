// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBButton } from '../../common/forms/fields';
import {withRouter} from 'react-router';
import { YBTextInput } from '../../common/forms/fields';
import {Field} from 'redux-form';
import {getPromiseState} from 'utils/PromiseUtils';
import {YBLoadingIcon} from '../../common/indicators';
import {isNonEmptyObject, isNonEmptyArray, convertSpaceToDash} from 'utils/ObjectUtils';
import { ProgressList } from '../../common/indicators';
import { DescriptionList } from '../../common/descriptors';
import { RegionMap, YBMapLegend } from '../../maps';
import { YBConfirmModal } from '../../modals';
import Dropzone from 'react-dropzone';

class GCPProviderConfiguration extends Component {
  constructor(props) {
    super(props);
    this.submitGCPConfiguration = this.submitGCPConfiguration.bind(this);
    this.state = {
      gcpConfig: {},
      accountName: "Google Cloud Provider",
      providerUUID: "",
      currentProvider: {},
      bootstrapSteps: [
        {type: "provider", name: "Create Provider", state: "Initializing"},
        {type: "region", name: "Create Region and Zones", state: "Initializing"},
        {type: "accessKey", name: "Create Access Key", state: "Initializing"},
        {type: "initialize", name: "Create Instance Types", state: "Initializing"}
      ]
    };
  }

  submitGCPConfiguration(vals) {
    const self = this;
    const configText = this.state.gcpConfig;
    if(isNonEmptyObject(configText)) {
      const providerName = vals.accountName;
      const reader = new FileReader();
      reader.readAsText(configText);
      // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
      reader.onloadend = function () {
        let gcpConfig = {};
        try {
          gcpConfig = JSON.parse(reader.result);
          self.props.createGCPProvider(providerName, gcpConfig);
        } catch (e) {
          self.setState({"error": "Invalid GCP config JSON file"});
        }
      };
    } else {
      this.setState({"error": "GCP Config JSON is required"});
    }
  }

  componentWillReceiveProps(nextProps) {
    const { cloudBootstrap: {data: { response, type }, error, promiseState}} = nextProps;
    if (promiseState.isSuccess() && this.props.cloudBootstrap.promiseState.isLoading()) {
      const { bootstrapSteps } = this.state;
      const currentStepIndex = bootstrapSteps.findIndex( (step) => step.type === type );
      if (currentStepIndex !== -1) {
        if (promiseState.isLoading()) {
          bootstrapSteps[currentStepIndex].state = "Running";
        } else {
          bootstrapSteps[currentStepIndex].state = error ? "Error" : "Success";
        }
        this.setState({bootstrapSteps: bootstrapSteps});
      }

      switch (type) {
        case "provider":
          const providerUUID = response.uuid;
          this.setState({providerUUID: providerUUID});
          const regionVals = {code: "us-west1", name: "us-west1"}; // Default for now
          this.props.createGCPRegions(providerUUID, regionVals);
          break;
        case "region":
          const keyInfo = {
            code: "yb-" + convertSpaceToDash(this.state.accountName.toLowerCase()) + "-key"
          };
          this.props.createGCPAccessKey(this.state.providerUUID, response.uuid, keyInfo);
          break;
        case "accessKey":
          this.props.initializeGCPMetadata(this.state.providerUUID);
          break;
        case "initialize":
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

  uploadGCPConfig(uploadFile) {
    this.setState({gcpConfig: uploadFile[0]});
  }

  render() {
    const { handleSubmit, configuredProviders, cloudBootstrap: {data: {type}, error}} = this.props;
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

    if (getPromiseState(configuredProviders).isLoading()) {
      return <YBLoadingIcon/>;
    }

    if (getPromiseState(configuredProviders).isSuccess()) {
      const gcpProvider = configuredProviders.data.find((provider) => provider.code === "gcp");
      if (isNonEmptyObject(gcpProvider)) {
        return <GCPConfigureSuccess {...this.props}/>;
      }
    }

    let gcpConfigFileName = "";
    if (isNonEmptyObject(this.state.gcpConfig)) {
      gcpConfigFileName = this.state.gcpConfig.name;
    }
    const errorString = JSON.stringify(error);
    return (
      <div className="provider-config-container">
        <form name="gcpConfigForm" onSubmit={handleSubmit(this.submitGCPConfiguration)}>
          { error && <Alert bsStyle="danger">{errorString}</Alert> }
          <div className="editor-container">
            <Row>
              <Col lg={8}>
                <Row className="config-provider-row">
                  <Col lg={2}>
                    <div className="form-item-custom-label">Name</div>
                  </Col>
                  <Col lg={10}>
                    <Field name="accountName" placeHolder="Google Cloud Platform"
                           component={YBTextInput} className={"gcp-provider-input-field"}/>
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={2}>
                    <div className="form-item-custom-label">Provider Config</div>
                  </Col>
                  <Col lg={6}>
                    <Dropzone onDrop={this.uploadGCPConfig.bind(this)} className="upload-file-button">
                      <p>Upload GCP Config json file</p>
                    </Dropzone>
                  </Col>
                  <Col lg={4}>
                    <div className="file-label">{gcpConfigFileName}</div>
                  </Col>
                </Row>
              </Col>
              <Col lg={4}>
                {bootstrapSteps}
              </Col>
            </Row>
          </div>
          <div className="form-action-button-container">
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"} btnType="submit"/>
          </div>
        </form>
      </div>
    );
  }
}

class GCPConfigureSuccess extends Component {

  constructor(props) {
    super(props);
    const { cloudBootstrap: {data: { type }, promiseState}} = props;
    this.state = {
      refreshing: type === "initialize" && promiseState.isLoading()
    };
    this.deleteProviderConfig = this.deleteProviderConfig.bind(this);
    this.refreshPricingData = this.refreshPricingData.bind(this);
  }

  deleteProviderConfig(provider) {
    this.props.deleteProviderConfig(provider.uuid);
  }

  refreshPricingData(provider) {
    this.props.initializeGCPMetadata(provider.uuid);
    this.setState({refreshing: true});
  }

  componentWillReceiveProps(nextProps) {
    const { cloudBootstrap: {data: { type }, promiseState}} = nextProps;
    const { refreshing } = this.state;
    if (refreshing && type === "initialize" && !promiseState.isLoading()) {
      this.setState({refreshing: false});
    }
  }

  render() {
    const {cloud: {supportedRegionList, accessKeys}, configuredProviders, universe: {universeList},
      handleSubmit} = this.props;
    const { refreshing } = this.state;
    const gcpProvider = configuredProviders.data.find((provider) => provider.code === "gcp");
    let gcpProviderName = "";
    let regions = [];
    let gcpKey = "No Key Configured";
    if (isNonEmptyObject(gcpProvider)) {
      gcpProviderName = gcpProvider.name;
      if (isNonEmptyArray(accessKeys.data)) {
        const accessKey = accessKeys.data.find((key) => {
          return key.idKey.providerUUID === gcpProvider.uuid;
        });
        if (isNonEmptyObject(accessKey)) {
          gcpKey = accessKey.idKey.keyCode;
        }
      }
      if (isNonEmptyArray(supportedRegionList.data)) {
        regions = supportedRegionList.data.filter((region) => region.provider.uuid === gcpProvider.uuid);
      }
    }

    let universeExistsForProvider = false;
    if (isNonEmptyArray(universeList.data)) {
      universeExistsForProvider = universeList.data.some(universe => universe.provider && (universe.provider.uuid === gcpProvider.uuid));
    }
    const deleteButtonDisabled = refreshing || universeExistsForProvider;
    const buttonBaseClassName = "btn btn-default manage-provider-btn";
    let deleteButtonClassName = buttonBaseClassName;
    let deleteButtonTitle = "Delete this GCP configuration.";
    if (deleteButtonDisabled) {
      deleteButtonTitle = "Can't delete this GCP configuration because one or more AWS universes are currently defined. Delete all GCP universes first.";
    } else {
      deleteButtonClassName += " delete-btn";
    }

    const providerInfo = [
      {name: "Name", data: gcpProviderName },
      {name: "SSH Key", data: gcpKey},
    ];

    let refreshPricingLabel = "Refresh Pricing Data";
    if (refreshing) {
      refreshPricingLabel = "Refreshing...";
    }

    return (
      <div className="provider-config-container">
        <Row className="config-section-header">
          <Col md={12}>
            <span className="pull-right" title={deleteButtonTitle}>
              <YBButton btnText="Delete Configuration" disabled={deleteButtonDisabled}
                        btnClass={deleteButtonClassName} onClick={this.props.showDeleteProviderModal}/>
              <YBButton btnText={refreshPricingLabel} btnClass={buttonBaseClassName}
                        disabled={refreshing} onClick={() => this.refreshPricingData(gcpProvider)} />
              <YBConfirmModal name="delete-aws-provider" title={"Confirm Delete"}
                              onConfirm={handleSubmit(() => this.deleteProviderConfig(gcpProvider))}
                              currentModal="deleteGCPProvider" visibleModal={this.props.visibleModal}
                              hideConfirmModal={this.props.hideDeleteProviderModal}>
                  Are you sure you want to delete this GCP configuration?
                </YBConfirmModal>
            </span>
            <DescriptionList listItems={providerInfo} />
          </Col>
        </Row>
        <Row>
          <Col lg={12} className="provider-map-container">
            <RegionMap title="All Supported Regions" regions={regions} type="Region" showLabels={true} showRegionLabels={true} />
            <YBMapLegend title="Region Map"/>
          </Col>
        </Row>
      </div>
    );
  }
}
export default withRouter(GCPProviderConfiguration);
