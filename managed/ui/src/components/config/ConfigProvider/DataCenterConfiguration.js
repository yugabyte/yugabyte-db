// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { withRouter } from 'react-router';
import {
  KubernetesProviderConfigurationContainer,
  OnPremConfigurationContainer,
  ProviderConfigurationContainer,
  StorageConfigurationContainer,
  SecurityConfiguration
} from '../../config';
import { Tab } from 'react-bootstrap';
import { YBTabsPanel, YBTabsWithLinksPanel } from '../../panels';
import awsLogo from './images/aws.svg';
import azureLogo from './images/azure.svg';
import k8sLogo from './images/k8s.png';
import openshiftLogo from './images/redhat.png';
import tanzuLogo from './images/tanzu.png';
import gcpLogo from './images/gcp.svg';
import { isAvailable, showOrRedirect } from '../../../utils/LayoutUtils';
import { NewStorageConfiguration } from '../Storage/StorageConfigurationNew';
import './DataCenterConfiguration.scss';

class DataCenterConfiguration extends Component {
  getTabTitle = (type) => {
    switch (type) {
      case 'AWS':
        return (
          <div className="title">
            <img src={awsLogo} alt="AWS" className="aws-logo" />
            <span>Amazon Web Services</span>
          </div>
        );
      case 'GCP':
        return (
          <div className="title">
            <img src={gcpLogo} alt="GCP" className="gcp-logo" />
            <span>Google Cloud Platform</span>
          </div>
        );
      case 'Azure':
        return (
          <div className="title">
            <img src={azureLogo} alt="Azure" className="azure-logo" />
            <span>Microsoft Azure</span>
          </div>
        );
      case 'Tanzu':
        return (
          <div className="title">
            <img src={tanzuLogo} alt="VMware Tanzu" />
            <span>VMware Tanzu</span>
          </div>
        );
      case 'Openshift':
        return (
          <div className="title">
            <img src={openshiftLogo} alt="Red Hat OpenShift" />
            <span>Red Hat OpenShift</span>
          </div>
        );
      case 'K8s':
        return (
          <div className="title">
            <img src={k8sLogo} alt="Managed Kubernetes" />
            <span>Managed Kubernetes Service</span>
          </div>
        );
      case 'Onprem':
        return (
          <div className="title">
            <i className="fa fa-server tab-logo" />
            <span>On-Premises Datacenters</span>
          </div>
        );
      default:
        return null;
    }
  };

  render() {
    const {
      customer: { currentCustomer },
      params: { tab, section },
      params,
      featureFlags
    } = this.props;
    showOrRedirect(currentCustomer.data.features, 'menu.config');
    const defaultTab = isAvailable(currentCustomer.data.features, 'config.infra')
      ? 'cloud'
      : 'backup';
    const activeTab = tab || defaultTab;
    const defaultConfig = section || 's3';

    return (
      <div>
        <h2 className="content-title">Cloud Provider Configuration</h2>
        <YBTabsWithLinksPanel
          defaultTab={defaultTab}
          activeTab={activeTab}
          routePrefix="/config/"
          id="config-tab-panel"
          className="universe-detail data-center-config-tab"
        >
          {isAvailable(currentCustomer.data.features, 'config.infra') && (
            <Tab eventKey="cloud" title="Infrastructure" key="cloud-config">
              <YBTabsPanel
                defaultTab="aws"
                activeTab={section}
                id="cloud-config-tab-panel"
                className="config-tabs"
                routePrefix="/config/cloud/"
              >
                <Tab
                  eventKey="aws"
                  title={this.getTabTitle('AWS')}
                  key="aws-tab"
                  unmountOnExit={true}
                >
                  <ProviderConfigurationContainer providerType="aws" />
                </Tab>
                <Tab
                  eventKey="gcp"
                  title={this.getTabTitle('GCP')}
                  key="gcp-tab"
                  unmountOnExit={true}
                >
                  <ProviderConfigurationContainer providerType="gcp" />
                </Tab>
                <Tab
                  eventKey="azure"
                  title={this.getTabTitle('Azure')}
                  key="azure-tab"
                  unmountOnExit={true}
                >
                  <ProviderConfigurationContainer providerType="azu" />
                </Tab>
                <Tab
                  eventKey="tanzu"
                  title={this.getTabTitle('Tanzu')}
                  key="tanzu-tab"
                  unmountOnExit={true}
                >
                  <KubernetesProviderConfigurationContainer type="tanzu" params={params} />
                </Tab>
                <Tab
                  eventKey="openshift"
                  title={this.getTabTitle('Openshift')}
                  key="openshift-tab"
                  unmountOnExit={true}
                >
                  <KubernetesProviderConfigurationContainer type="openshift" params={params} />
                </Tab>
                <Tab
                  eventKey="k8s"
                  title={this.getTabTitle('K8s')}
                  key="k8s-tab"
                  unmountOnExit={true}
                >
                  <KubernetesProviderConfigurationContainer type="k8s" params={params} />
                </Tab>
                <Tab
                  eventKey="onprem"
                  title={this.getTabTitle('Onprem')}
                  key="onprem-tab"
                  unmountOnExit={true}
                >
                  <OnPremConfigurationContainer params={params} />
                </Tab>
              </YBTabsPanel>
            </Tab>
          )}
          {isAvailable(currentCustomer.data.features, 'config.backup') && (
            <Tab eventKey="backup" title="Backup" key="storage-config">
              <StorageConfigurationContainer activeTab={defaultConfig} />
            </Tab>
          )}
          {isAvailable(currentCustomer.data.features, 'config.security') && (
            <Tab eventKey="security" title="Security" key="security-config">
              <SecurityConfiguration activeTab={section} />
            </Tab>
          )}
          {(featureFlags.test['enableMultiRegionConfig'] ||
            featureFlags.released['enableMultiRegionConfig']) && (
            <Tab eventKey="newBackupConfig" title="New Backup Config" key="new-backup-config">
              <NewStorageConfiguration activeTab={section} />
            </Tab>
          )}
        </YBTabsWithLinksPanel>
      </div>
    );
  }
}
export default withRouter(DataCenterConfiguration);
