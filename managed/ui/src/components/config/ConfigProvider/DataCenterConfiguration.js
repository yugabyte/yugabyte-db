// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { withRouter, browserHistory } from 'react-router';
import { AzureProviderConfigurationContainer, KubernetesProviderConfigurationContainer,
         OnPremConfigurationContainer, ProviderConfigurationContainer, StorageConfigurationContainer } from '../../config';
import { Tab, Row, Col } from 'react-bootstrap';
import { YBTabsPanel } from '../../panels';
import './providerConfig.scss';
import awsLogo from './images/aws.svg';
import azureLogo from './images/azure.png';
import k8sLogo from './images/k8s.png';
import pksLogo from './images/pks.png';
import gcpLogo from './images/gcp.png';
import { isNonAvailable } from 'utils/LayoutUtils';


class DataCenterConfiguration extends Component {

  componentWillMount() {
    const { customer: { currentCustomer }} = this.props;
    if (isNonAvailable(currentCustomer.data.features, "alerts.display")) browserHistory.push('/');
  }

  configProviderSelect = item => {
    const currentLocation = this.props.location;
    currentLocation.query = { provider: item };
    this.props.router.push(currentLocation);
  };

  render() {
    const onPremiseTabContent = (
      <div className="on-premise">
        <i className="fa fa-server" />
        On-Premises<br/>Datacenters
      </div>
    );

    const k8sTabContent = (
      <Row className="custom-tab">
        <Col md={4}>
          <img src={k8sLogo} alt="Managed Kubernetes" className="k8s-logo" />
        </Col>
        <Col md={8}>
          Managed<br/>Kubernetes Service
        </Col>
      </Row>
    );

    const pksTabContent = (
      <Row className="custom-tab">
        <Col md={4} >
          <img src={pksLogo} alt="Pivotal Container Service" className="pks-logo" />
        </Col>
        <Col md={8} className="provider-map-container">
          Pivotal<br/>Container Service
        </Col>
      </Row>
    );


    return (
      <div>
        <h2 className="content-title">Cloud Provider Configuration</h2>
        <YBTabsPanel defaultTab="cloud" activeTab={this.props.params.tab} routePrefix="/config/" id="config-tab-panel">
          <Tab eventKey="cloud" title="Infrastructure" key="cloud-config">
            <YBTabsPanel defaultTab="aws" activeTab={this.props.params.section} id="cloud-config-tab-panel" className="config-tabs" routePrefix="/config/cloud/">
              <Tab eventKey="aws" title={<img src={awsLogo} alt="AWS" className="aws-logo" />} key="aws-tab" unmountOnExit={true}>
                <ProviderConfigurationContainer providerType="aws" />
              </Tab>
              <Tab eventKey="gcp" title={<img src={gcpLogo} alt="GCP" className="gcp-logo" />} key="gcp-tab" unmountOnExit={true}>
                <ProviderConfigurationContainer providerType="gcp" />
              </Tab>
              <Tab eventKey="azure" title={<img src={azureLogo} alt="Azure" className="azure-logo" />} key="azure-tab" unmountOnExit={true}>
                <AzureProviderConfigurationContainer />
              </Tab>
              <Tab eventKey="pks" title={pksTabContent} key="pks-tab" unmountOnExit={true}>
                <KubernetesProviderConfigurationContainer type="pks" params={this.props.params} />
              </Tab>
              <Tab eventKey="k8s" title={k8sTabContent} key="k8s-tab" unmountOnExit={true}>
                <KubernetesProviderConfigurationContainer type="k8s" params={this.props.params} />
              </Tab>
              <Tab eventKey="onprem" title={onPremiseTabContent} key="onprem-tab" unmountOnExit={true}>
                <OnPremConfigurationContainer params={this.props.params} />
              </Tab>
            </YBTabsPanel>
          </Tab>
          <Tab eventKey="backup" title="Backup" key="storage-config">
            <StorageConfigurationContainer activeTab={this.props.params.section} />
          </Tab>
        </YBTabsPanel>
      </div>
    );
  }
}
export default withRouter(DataCenterConfiguration);
