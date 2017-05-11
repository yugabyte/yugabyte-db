// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Alert } from 'react-bootstrap';
import { OnPremConfigWizardContainer, OnPremConfigJSONContainer, AddHostDataFormContainer } from '../../config';
import { YBButton } from '../../common/forms/fields';
import emptyDataCenterConfig from '../templates/EmptyDataCenterConfig.json';
import { isValidObject } from '../../../utils/ObjectUtils';

export default class OnPremConfiguration extends Component {

  constructor(props) {
    super(props);
    const emptyJsonPretty = JSON.stringify(JSON.parse(JSON.stringify(emptyDataCenterConfig)), null, 2);
    this.state = {
      isJsonEntry: false,
      isAdditionalHostOptionsOpen: false,
      configJsonVal: emptyJsonPretty,
      succeeded: false,
      bootstrapSteps: [
        {type: "provider", name: "Create Provider", status: "Initializing"},
        {type: "instanceType", name: "Create Instance Types", status: "Initializing"},
        {type: "region", name: "Create Regions", status: "Initializing"},
        {type: "zone", name: "Create Zones", status: "Initializing"},
        {type: "node", name: "Create Node Instances", status: "Initializing"},
        {type: "accessKey", name: "Create Access Keys", status: "Initializing"}
      ],
      regionsMap: {},
      zonesMap: {},
      providerUUID: null,
      numZones: 0,
      numNodesConfigured: 0,
      numAccessKeysConfigured: 0
    };
    this.toggleJsonEntry = this.toggleJsonEntry.bind(this);
    this.toggleAdditionalOptionsModal = this.toggleAdditionalOptionsModal.bind(this);
    this.submitJson = this.submitJson.bind(this);
    this.updateConfigJsonVal = this.updateConfigJsonVal.bind(this);
  }

  componentWillReceiveProps(nextProps) {
    const { cloudBootstrap: { loading, response, error, type }} = nextProps;
    let bootstrapSteps = this.state.bootstrapSteps;
    let currentStepIndex = bootstrapSteps.findIndex( (step) => step.type === type );
    if (currentStepIndex !== -1) {
      if (loading) {
        bootstrapSteps[currentStepIndex].status = "Running";
      } else {
        bootstrapSteps[currentStepIndex].status = error ? "Error" : "Success";
      }
      this.setState({bootstrapSteps: bootstrapSteps});
    }

    if (isValidObject(response)) {
      const config = JSON.parse(this.state.configJsonVal);
      const numZones = config.regions.reduce((total, region) => {
        return total + region.zones.length
      }, 0);
      switch (type) {
        case "provider":
          // Launch configuration of instance types
          this.setState({
            succeeded: false,
            regionsMap: {},
            zonesMap: {},
            providerUUID: response.uuid,
            numZones: numZones,
            numNodesConfigured: 0,
            numAccessKeysConfigured: 0
          });
          bootstrapSteps[currentStepIndex + 1].status = "Running";
          this.props.createOnPremInstanceTypes(response.uuid, config);
          break;
        case "instanceType":
          // Launch configuration of regions
          bootstrapSteps[currentStepIndex + 1].status = "Running";
          this.props.createOnPremRegions(this.state.providerUUID, config);
          break;
        case "region":
          // Update regionsMap until done
          let regionsMap = this.state.regionsMap;
          regionsMap[response.code] = response.uuid;
          this.setState({regionsMap: regionsMap});
          // Launch configuration of zones once all regions are bootstrapped
          if (Object.keys(regionsMap).length === config.regions.length) {
            bootstrapSteps[currentStepIndex + 1].status = "Running";
            this.props.createOnPremZones(this.state.providerUUID, regionsMap, config);
          }
          break;
        case "zone":
          // Update zonesMap until done
          let zonesMap = this.state.zonesMap;
          zonesMap[response.code] = response.uuid;
          this.setState({zonesMap: zonesMap});
          // Launch configuration of node instances once all availability zones are bootstrapped
          if (Object.keys(zonesMap).length === this.state.numZones) {
            bootstrapSteps[currentStepIndex + 1].status = "Running";
            this.props.createOnPremNodes(zonesMap, config);
          }
          break;
        case "node":
          // Update numNodesConfigured until done
          let numNodesConfigured = this.state.numNodesConfigured;
          numNodesConfigured++;
          this.setState({numNodesConfigured: numNodesConfigured});
          // Launch configuration of access keys once all node instances are bootstrapped
          if (numNodesConfigured === config.nodes.length) {
            bootstrapSteps[currentStepIndex + 1].status = "Running";
            this.props.createOnPremAccessKeys(this.state.providerUUID, this.state.regionsMap, config);
          }
          break;
        case "accessKey":
          // Update numAccessKeysConfigured until done
          let numAccessKeysConfigured = this.state.numAccessKeysConfigured;
          numAccessKeysConfigured++;
          const succeeded = this.state.succeeded || numAccessKeysConfigured === config.regions.length;
          this.setState({numAccessKeysConfigured: numAccessKeysConfigured, succeeded: succeeded});
          // When finished, display success message & update app's provider list
          if (succeeded) {
            this.props.onPremConfigSuccess();
          }
          break;
        default:
          break;
      }
    }
  }

  toggleJsonEntry() {
    this.setState({'isJsonEntry': !this.state.isJsonEntry})
  }

  toggleAdditionalOptionsModal() {
    this.setState({isAdditionalHostOptionsOpen: !this.state.isAdditionalHostOptionsOpen});
  }

  updateConfigJsonVal(newConfigJsonVal) {
    this.setState({configJsonVal: newConfigJsonVal});
  }

  submitJson() {
    if (this.state.isJsonEntry) {
      this.props.createOnPremProvider(JSON.parse(this.state.configJsonVal));
    }
  }

  render() {
    const { cloudBootstrap } = this.props;
    let ConfigurationDataForm = <OnPremConfigWizardContainer />;
    let btnText = this.state.isJsonEntry ? "Switch To Wizard View" : "Switch To JSON View";
    if (this.state.isJsonEntry) {
      ConfigurationDataForm = <OnPremConfigJSONContainer updateConfigJsonVal={this.updateConfigJsonVal}
                                                         configJsonVal={this.state.configJsonVal} />
    }
    let hostDataForm = <AddHostDataFormContainer visible={this.state.isAdditionalHostOptionsOpen}
                                                 onHide={this.toggleAdditionalOptionsModal}/>;

    let message = "";
    if (this.state.succeeded) {
      message = <Alert bsStyle="success">Create On Premise Provider Succeeded</Alert>
    } else if (cloudBootstrap && cloudBootstrap.error) {
      message = <Alert bsStyle="danger">Create On Premise Provider Failed</Alert>
    }

    return (
      <div className="on-prem-provider-container">
        {message}
        {hostDataForm}
        {ConfigurationDataForm}
        <div className="form-action-button-container">
          <YBButton btnText={btnText} btnClass={"btn btn-default"} onClick={this.toggleJsonEntry}/>
          <YBButton btnText={"Additional Host Options"} onClick={this.toggleAdditionalOptionsModal}/>
          <YBButton btnClass="pull-right btn btn-default bg-orange" btnText={"Save"} onClick={this.submitJson}/>
        </div>
      </div>
    )
  }
}
