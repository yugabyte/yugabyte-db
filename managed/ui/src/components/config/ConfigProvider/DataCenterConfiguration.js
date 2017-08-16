// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { withRouter } from 'react-router';
import { GCPProviderConfigurationContainer, AzureProviderConfigurationContainer,
         DockerProviderConfigurationContainer, AWSProviderConfigurationContainer, OnPremConfigurationContainer}
         from '../../config';
import {Tab} from 'react-bootstrap';
import { YBTabsPanel } from '../../panels';
import './providerConfig.scss';
import awsLogo from './images/aws.png';
import azureLogo from './images/azure.png';
import dockerLogo from './images/docker.png';
import gcpLogo from './images/gcp.png';

class DataCenterConfiguration extends Component {
  constructor(props) {
    super(props);
    this.configProviderSelect = this.configProviderSelect.bind(this);
  }

  configProviderSelect(item) {
    let currentLocation = this.props.location;
    currentLocation.query = { provider: item };
    this.props.router.push(currentLocation);
  }

  render() {
    const onPremiseTabContent = (
      <div className="on-premise">
        <i className="fa fa-server" />
        On-Premises<br/>Datacenters
      </div>
    );
    return (
      <div>
        <h2>Cloud Provider Configuration</h2>
        <YBTabsPanel defaultTab="aws" activeTab={this.props.params.tab} id="config-tab-panel" className="config-tabs" routePrefix="/config/">
          <Tab eventKey="aws" title={<img src={awsLogo} alt="AWS" className="aws-logo" />} key="aws-tab" unmountOnExit={true}>
            <AWSProviderConfigurationContainer />
          </Tab>
          <Tab eventKey="gcp" title={<img src={gcpLogo} alt="GCP" className="gcp-logo" />} key="gcp-tab" unmountOnExit={true}>
            <GCPProviderConfigurationContainer />
          </Tab>
          <Tab eventKey="azure" title={<img src={azureLogo} alt="Azure" className="azure-logo" />} key="azure-tab" unmountOnExit={true}>
            <AzureProviderConfigurationContainer />
          </Tab>
          <Tab eventKey="docker" title={<img src={dockerLogo} alt="Docker" className="docker-logo" />} key="docker-tab" unmountOnExit={true}>
            <DockerProviderConfigurationContainer />
          </Tab>
          <Tab eventKey="onprem" title={onPremiseTabContent} key="onprem-tab" unmountOnExit={true}>
            <OnPremConfigurationContainer params={this.props.params} />
          </Tab>
        </YBTabsPanel>
      </div>
    )
  }
}
export default withRouter(DataCenterConfiguration);
