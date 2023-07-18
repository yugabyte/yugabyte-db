// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Tab } from 'react-bootstrap';
import { YBTabsPanel } from '../../panels';
import KeyManagementConfigurationContainer from './KeyManagementConfigurationContainer';
import { CertificatesContainer } from './certificates';
import encryptionTransitIcon from './images/encryption-transit-icon.png';
import encryptionRestIcon from './images/encryption-rest-icon.png';

const EncryptionAtRestTabHeader = (
  <span>
    <img src={encryptionRestIcon} alt="Encryption at Rest" className="encryption-rest-icon" />
    Encryption At Rest
  </span>
);

const CertificatesTabHeader = (
  <span>
    <img
      src={encryptionTransitIcon}
      alt="Encryption in Transit"
      className="encryption-transit-icon"
    />
    Encryption In Transit
  </span>
);

const TAB_AT_REST = 'encryption-at-rest';
const TAB_IN_TRANSIT = 'encryption-in-transit';

class SecurityConfiguration extends Component {
  render() {
    const activeTab = this.props.activeTab ?? TAB_AT_REST;
    const routePrefix = this.props.routePrefix ?? '/config/security/';
    return (
      <YBTabsPanel
        defaultTab={TAB_AT_REST}
        activeTab={activeTab}
        id="security-tab-panel"
        className="config-tabs"
        routePrefix={routePrefix}
      >
        <Tab eventKey={TAB_AT_REST} title={EncryptionAtRestTabHeader}>
          <KeyManagementConfigurationContainer />
        </Tab>
        <Tab eventKey={TAB_IN_TRANSIT} title={CertificatesTabHeader}>
          <CertificatesContainer />
        </Tab>
      </YBTabsPanel>
    );
  }
}
export default SecurityConfiguration;
