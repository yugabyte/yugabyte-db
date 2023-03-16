/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import { Tab } from 'react-bootstrap';
import { useSelector } from 'react-redux';

import SecurityConfiguration from '../config/Security/SecurityConfiguration';
import awsLogo from '../config/ConfigProvider/images/aws.svg';
import azureLogo from '../config/ConfigProvider/images/azure.svg';
import gcpLogo from '../config/ConfigProvider/images/gcp.svg';
import k8sLogo from '../config/ConfigProvider/images/k8s.png';
import openshiftLogo from '../config/ConfigProvider/images/redhat.png';
import tanzuLogo from '../config/ConfigProvider/images/tanzu.png';
import { InfraProvider } from './providerRedesign/InfraProvider';
import {
  ConfigTabKey,
  CONFIG_ROUTE_PREFIX,
  KubernetesProviderType,
  ProviderCode
} from './providerRedesign/constants';
import { LocationShape } from 'react-router/lib/PropTypes';
import { NewStorageConfiguration } from '../config/Storage/StorageConfigurationNew';
import { ProviderView } from './providerRedesign/providerView/ProviderView';
import { StorageConfigurationContainer } from '../config';
import { YBErrorIndicator } from '../common/indicators';
import { YBTabsPanel, YBTabsWithLinksPanel } from '../panels';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';
import { isAvailable, showOrRedirect } from '../../utils/LayoutUtils';

interface ReactRouterProps {
  location: LocationShape;
  params: { tab?: string; section?: string; uuid?: string };
}

export const DataCenterConfigRedesign = ({ location, params }: ReactRouterProps) => {
  const { currentCustomer } = useSelector((state: any) => state.customer);
  const featureFlags = useSelector((state: any) => state.featureFlags);

  showOrRedirect(currentCustomer.data.features, 'menu.config');

  // Validate URL param
  if (
    params.tab !== undefined &&
    !Object.values(ConfigTabKey).includes(params.tab as ConfigTabKey)
  ) {
    return <YBErrorIndicator customErrorMessage="404 Page Not Found." />;
  }

  const defaultTab = isAvailable(currentCustomer.data.features, 'config.infra')
    ? ConfigTabKey.INFRA
    : ConfigTabKey.BACKUP;
  const activeTab = params.tab ?? defaultTab;
  const activeSection = params.section ?? 's3';
  return (
    <div>
      <h2 className="content-title">Provider Configuration</h2>
      <YBTabsWithLinksPanel
        defaultTab={defaultTab}
        activeTab={activeTab}
        routePrefix={`/${CONFIG_ROUTE_PREFIX}/`}
        id="config-tab-panel"
        className="universe-detail data-center-config-tab"
      >
        {isAvailable(currentCustomer.data.features, 'config.infra') && (
          <Tab eventKey={ConfigTabKey.INFRA} title="Infrastructure" key="infra-config">
            <YBTabsPanel
              defaultTab={ProviderCode.AWS}
              activeTab={params.section}
              id="cloud-config-tab-panel"
              className="config-tabs redesign"
              routePrefix={`/${CONFIG_ROUTE_PREFIX}/${ConfigTabKey.INFRA}/`}
            >
              <Tab
                eventKey={ProviderCode.AWS}
                title={getTabTitle(ProviderCode.AWS)}
                key="aws-tab"
                unmountOnExit={true}
              >
                {params.uuid === undefined ? (
                  <InfraProvider providerCode={ProviderCode.AWS} />
                ) : (
                  <ProviderView providerUUID={params.uuid} />
                )}
              </Tab>
              <Tab
                eventKey={ProviderCode.GCP}
                title={getTabTitle(ProviderCode.GCP)}
                key="gcp-tab"
                unmountOnExit={true}
              >
                {params.uuid === undefined ? (
                  <InfraProvider providerCode={ProviderCode.GCP} />
                ) : (
                  <ProviderView providerUUID={params.uuid} />
                )}
              </Tab>
              <Tab
                eventKey={ProviderCode.AZU}
                title={getTabTitle(ProviderCode.AZU)}
                key="azure-tab"
                unmountOnExit={true}
              >
                {params.uuid === undefined ? (
                  <InfraProvider providerCode={ProviderCode.AZU} />
                ) : (
                  <ProviderView providerUUID={params.uuid} />
                )}
              </Tab>
              <Tab
                eventKey={KubernetesProviderType.TANZU}
                title={getTabTitle(KubernetesProviderType.TANZU)}
                key="tanzu-tab"
                unmountOnExit={true}
              >
                {params.uuid === undefined ? (
                  <InfraProvider
                    providerCode={ProviderCode.KUBERNETES}
                    kubernetesProviderType={KubernetesProviderType.TANZU}
                  />
                ) : (
                  <ProviderView providerUUID={params.uuid} />
                )}
              </Tab>
              <Tab
                eventKey={KubernetesProviderType.OPEN_SHIFT}
                title={getTabTitle(KubernetesProviderType.OPEN_SHIFT)}
                key="openshift-tab"
                unmountOnExit={true}
              >
                {params.uuid === undefined ? (
                  <InfraProvider
                    providerCode={ProviderCode.KUBERNETES}
                    kubernetesProviderType={KubernetesProviderType.OPEN_SHIFT}
                  />
                ) : (
                  <ProviderView providerUUID={params.uuid} />
                )}
              </Tab>
              <Tab
                eventKey={KubernetesProviderType.MANAGED_SERVICE}
                title={getTabTitle(KubernetesProviderType.MANAGED_SERVICE)}
                key="k8s-tab"
                unmountOnExit={true}
              >
                {params.uuid === undefined ? (
                  <InfraProvider
                    providerCode={ProviderCode.KUBERNETES}
                    kubernetesProviderType={KubernetesProviderType.MANAGED_SERVICE}
                  />
                ) : (
                  <ProviderView providerUUID={params.uuid} />
                )}
              </Tab>
              <Tab
                eventKey={ProviderCode.ON_PREM}
                title={getTabTitle(ProviderCode.ON_PREM)}
                key="onprem-tab"
                unmountOnExit={true}
              >
                {params.uuid === undefined ? (
                  <InfraProvider providerCode={ProviderCode.ON_PREM} />
                ) : (
                  <ProviderView providerUUID={params.uuid} />
                )}
              </Tab>
            </YBTabsPanel>
          </Tab>
        )}
        {isAvailable(currentCustomer.data.features, 'config.backup') && (
          <Tab eventKey="backup" title="Backup" key="storage-config">
            <StorageConfigurationContainer
              activeTab={activeSection}
              routePrefix={CONFIG_ROUTE_PREFIX}
            />
          </Tab>
        )}
        {isAvailable(currentCustomer.data.features, 'config.security') && (
          <Tab eventKey={ConfigTabKey.SECURITY} title="Security" key="security-config">
            <SecurityConfiguration activeTab={params.section} />
          </Tab>
        )}
        {(featureFlags.test['enableMultiRegionConfig'] ||
          featureFlags.released['enableMultiRegionConfig']) && (
          <Tab eventKey={ConfigTabKey.BACKUP_NEW} title="New Backup Config" key="new-backup-config">
            <NewStorageConfiguration activeTab={params.section} />
          </Tab>
        )}
      </YBTabsWithLinksPanel>
    </div>
  );
};

const getTabTitle = (providerCode: ProviderCode | KubernetesProviderType) => {
  switch (providerCode) {
    case ProviderCode.AWS:
      return (
        <div className="title">
          <img src={awsLogo} alt="AWS" className="aws-logo" />
          <span>Amazon Web Services</span>
        </div>
      );
    case ProviderCode.GCP:
      return (
        <div className="title">
          <img src={gcpLogo} alt="GCP" className="gcp-logo" />
          <span>Google Cloud Platform</span>
        </div>
      );
    case ProviderCode.AZU:
      return (
        <div className="title">
          <img src={azureLogo} alt="Azure" className="azure-logo" />
          <span>Microsoft Azure</span>
        </div>
      );
    case KubernetesProviderType.TANZU:
      return (
        <div className="title">
          <img src={tanzuLogo} alt="VMware Tanzu" />
          <span>VMware Tanzu</span>
        </div>
      );
    case KubernetesProviderType.OPEN_SHIFT:
      return (
        <div className="title">
          <img src={openshiftLogo} alt="Red Hat OpenShift" />
          <span>Red Hat OpenShift</span>
        </div>
      );
    case KubernetesProviderType.MANAGED_SERVICE:
    case ProviderCode.KUBERNETES:
      return (
        <div className="title">
          <img src={k8sLogo} alt="Managed Kubernetes" />
          <span>Managed Kubernetes Service</span>
        </div>
      );
    case ProviderCode.ON_PREM:
      return (
        <div className="title">
          <i className="fa fa-server tab-logo" />
          <span>On-Premises Datacenters</span>
        </div>
      );
    case ProviderCode.CLOUD:
    case ProviderCode.DOCKER:
    case ProviderCode.OTHER:
    case ProviderCode.UNKNOWN:
    case KubernetesProviderType.DEPRECATED:
      // Unsupported provider types.
      return null;
    default:
      return assertUnreachableCase(providerCode);
  }
};
