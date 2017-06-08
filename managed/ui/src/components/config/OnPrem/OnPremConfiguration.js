// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Alert } from 'react-bootstrap';
import _ from 'lodash';
import { isValidObject, isDefinedNotNull, isNonEmptyArray, isNonEmptyObject } from 'utils/ObjectUtils';
import { OnPremConfigWizardContainer, OnPremConfigJSONContainer, OnPremSuccessContainer } from '../../config';
import { YBButton } from '../../common/forms/fields';
import emptyDataCenterConfig from '../templates/EmptyDataCenterConfig.json';
import './OnPremConfiguration.scss';
const PROVIDER_TYPE = "onprem";
import {getPromiseState} from 'utils/PromiseUtils';
import {YBLoadingIcon} from '../../common/indicators';

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
    this.submitWizardJson = this.submitWizardJson.bind(this);
  }

  componentWillMount() {
    this.props.resetConfigForm();
  }

  componentWillReceiveProps(nextProps) {
    const { cloudBootstrap: {data: { response, type }, error, promiseState}} = nextProps;
    let bootstrapSteps = this.state.bootstrapSteps;
    let currentStepIndex = bootstrapSteps.findIndex( (step) => step.type === type );
    if (currentStepIndex !== -1) {
      if (promiseState.isLoading()) {
        bootstrapSteps[currentStepIndex].status = "Running";
      } else {
        bootstrapSteps[currentStepIndex].status = error ? "Error" : "Success";
      }
      this.setState({bootstrapSteps: bootstrapSteps});
    }

    if (isValidObject(response)) {
      const config = _.isString(this.state.configJsonVal) ? JSON.parse(this.state.configJsonVal) : this.state.configJsonVal;
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
          this.props.createOnPremRegions(response.uuid, config);
          this.props.createOnPremInstanceTypes(PROVIDER_TYPE, response.uuid, config);
          break;
        case "instanceType":
          // Launch configuration of regions
          bootstrapSteps[currentStepIndex + 1].status = "Running";

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
            // If nodes specified create nodes, else jump to access key
            if (isNonEmptyArray(config.nodes)) {
              this.props.createOnPremNodes(zonesMap, config);
            } else {
              this.props.createOnPremAccessKeys(this.state.providerUUID, this.state.regionsMap, config);
            }
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
            if (config.key && isNonEmptyObject(config.key.privateKeyContent)) {
              this.props.createOnPremAccessKeys(this.state.providerUUID, this.state.regionsMap, config);
            }
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
      this.props.createOnPremProvider(PROVIDER_TYPE, JSON.parse(this.state.configJsonVal));
    }
  }

  submitWizardJson(payloadData) {
    this.setState({configJsonVal: payloadData})
    this.props.createOnPremProvider(PROVIDER_TYPE, payloadData);
  }

  render() {
    const { configuredProviders } = this.props;
    if (getPromiseState(configuredProviders).isInit() || getPromiseState(configuredProviders).isError()) {
      return <span/>;
    }
    else if (getPromiseState(configuredProviders).isLoading()) {
      return <YBLoadingIcon/>;
    } else if (getPromiseState(configuredProviders).isSuccess()) {
      var providerFound = configuredProviders.data.find(provider => provider.code === 'onprem');
      if (isDefinedNotNull(providerFound)) {
        return <OnPremSuccessContainer/>;
      }
    }
    var switchToJsonEntry = <YBButton btnText={"Switch to JSON View"} btnClass={"btn btn-default pull-left"} onClick={this.toggleJsonEntry}/>;
    var switchToWizardEntry = <YBButton btnText={"Switch to Wizard View"} btnClass={"btn btn-default pull-left"} onClick={this.toggleJsonEntry}/>;
    let ConfigurationDataForm = <OnPremConfigWizardContainer switchToJsonEntry={switchToJsonEntry} submitWizardJson={this.submitWizardJson}/>;
    if (this.state.isJsonEntry) {
      ConfigurationDataForm = <OnPremConfigJSONContainer updateConfigJsonVal={this.updateConfigJsonVal}
                                                         configJsonVal={_.isString(this.state.configJsonVal) ? this.state.configJsonVal : JSON.stringify(JSON.parse(JSON.stringify(this.state.configJsonVal)), null, 2)}
                                                         switchToWizardEntry={switchToWizardEntry} submitJson={this.submitJson}/>
    }
    return (
      <div className="on-prem-provider-container">
        {ConfigurationDataForm}
      </div>
    )
  }
}
