// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Tab } from 'react-bootstrap';
import { YBTabsPanel } from '../../panels';
import KeyManagementConfigurationContainer from './KeyManagementConfigurationContainer';

const securitySubgroups = ['Encryption At Rest'];

class SecurityConfiguration extends Component {
  render() {
    const activeTab = this.props.activeTab || securitySubgroups[0].toLowerCase();
    const tabHeader = (
      <div className="on-premise">
        <i className="fa fa-lock"></i>Encryption <br /> At Rest
      </div>
    );
    return (
      <YBTabsPanel
        defaultTab={securitySubgroups[0].toLowerCase()}
        activeTab={activeTab}
        id="storage-config-tab-panel"
        className="config-tabs"
        routePrefix="/config/backup/"
      >
        <Tab eventKey={'encryption at rest'} title={tabHeader} key={'encryption-at-rest-tab'}>
          <KeyManagementConfigurationContainer />
        </Tab>
      </YBTabsPanel>
    );
  }
}
export default SecurityConfiguration;
