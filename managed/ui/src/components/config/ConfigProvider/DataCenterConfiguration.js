// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { withRouter } from 'react-router';
import { GCPProviderConfigurationContainer, AzureProviderConfigurationContainer,
         DockerProviderConfigurationContainer, AWSProviderConfigurationContainer}
         from '../../config';
import {OnPremConfiguration} from '../../config';
import {Tab} from 'react-bootstrap';
import { YBTabsPanel } from '../../panels';
import './providerConfig.scss';

class DataCenterConfiguration extends Component {
  constructor(props) {
    super(props);
    this.configProviderSelect = this.configProviderSelect.bind(this);
  }

  configProviderSelect(item) {
    let currentLocation = this.props.location;
    currentLocation.query = { provider: item }
    this.props.router.push(currentLocation);
  }

  render() {
    return (
      <div>
        <h2>Configuration</h2>
        <YBTabsPanel activeTab={"gcp"} id={"universe-tab-panel"}>
          <Tab eventKey={"aws"} title="AWS" key="aws-tab">
            <AWSProviderConfigurationContainer />
          </Tab>
          <Tab eventKey={"gcp"} title="GCP" key="gcp-tab">
            <GCPProviderConfigurationContainer />
          </Tab>
          <Tab eventKey={"docker"} title="Docker" key="docker-tab">
            <DockerProviderConfigurationContainer />
          </Tab>
          <Tab eventKey={"onprem"} title="OnPrem" key="onprem-tab">
            <OnPremConfiguration/>
          </Tab>
          <Tab eventKey={"azure"} title="Azure" key="azure-tab">
            <AzureProviderConfigurationContainer />
          </Tab>
        </YBTabsPanel>
      </div>
    )
  }
}
export default withRouter(DataCenterConfiguration);
