/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { Tab } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import { useQuery, useQueries } from 'react-query';
import i18next from 'i18next';
import { useTranslation } from 'react-i18next';

import SecurityConfiguration from '../config/Security/SecurityConfiguration';
import awsLogo from '../config/ConfigProvider/images/aws.svg';
import azureLogo from '../config/ConfigProvider/images/azure.svg';
import gcpLogo from '../config/ConfigProvider/images/gcp.svg';
import k8sLogo from '../config/ConfigProvider/images/k8s.png';
import openshiftLogo from '../config/ConfigProvider/images/redhat.png';
import tanzuLogo from '../config/ConfigProvider/images/tanzu.png';
import { InfraProvider } from './providerRedesign/InfraProvider';
import {
  CONFIG_ROUTE_PREFIX,
  CloudVendorProviders,
  ConfigTabKey,
  KubernetesProviderType,
  ProviderCode,
  SUPPORTED_KUBERNETES_PROVIDERS
} from './providerRedesign/constants';
import { LocationShape } from 'react-router/lib/PropTypes';
import { NewStorageConfiguration } from '../config/Storage/StorageConfigurationNew';
import { ProviderView } from './providerRedesign/providerView/ProviderView';
import { StorageConfigurationContainer } from '../config';
import { ExportLog } from '../../redesign/features/export-log/ExportLog';
import { YBErrorIndicator } from '../common/indicators';
import { YBTabsPanel, YBTabsWithLinksPanel } from '../panels';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';
import { isAvailable, showOrRedirect } from '../../utils/LayoutUtils';
import { api, regionMetadataQueryKey } from '../../redesign/helpers/api';
import { RbacValidator } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';
import { TroubleshootingDetails } from '../../redesign/features/Troubleshooting/TroubleshootingDetails';
import { fetchGlobalRunTimeConfigs } from '../../api/admin';
import { runtimeConfigQueryKey } from '../../redesign/helpers/api';
import { RuntimeConfigKey } from '../../redesign/helpers/constants';

interface ReactRouterProps {
  location: LocationShape;
  params: { tab?: string; section?: string; uuid?: string };
  isTroubleshootingEnabled: boolean;
}

export const DataCenterConfigRedesign = ({
  location,
  params,
  isTroubleshootingEnabled
}: ReactRouterProps) => {
  const { currentCustomer } = useSelector((state: any) => state.customer);
  const featureFlags = useSelector((state: any) => state.featureFlags);
  showOrRedirect(currentCustomer.data.features, 'menu.config');
  const { t } = useTranslation('translation', { keyPrefix: 'integrations' });

  // Start fetching the region metadata to seed the cache once the user navigates to the providers tab.
  // Although this data isn't required on first load, the intention is to start fetching in the background to avoid
  // showing loading spinners when the user starts configuring regions.
  // The region metadata is simply .yml files stored on the server and does not change at runtime so it is safe to load
  // in advance.
  // This also makes sure we keep the cached region metadata until the user navigates away from the providers tab.
  useQueries(
    CloudVendorProviders.map((providerCode) => ({
      queryKey: regionMetadataQueryKey.detail(providerCode),
      queryFn: () => api.fetchRegionMetadata(providerCode)
    }))
  );

  useQueries(
    SUPPORTED_KUBERNETES_PROVIDERS.map((kubernetesProvider) => ({
      queryKey: regionMetadataQueryKey.detail(ProviderCode.KUBERNETES, kubernetesProvider),
      queryFn: () => api.fetchRegionMetadata(ProviderCode.KUBERNETES, kubernetesProvider)
    }))
  );

  const globalRuntimeConfigs = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );

  const isExportLogEnabled =
    globalRuntimeConfigs?.data?.configEntries?.find(
      (c: any) => c.key === RuntimeConfigKey.ENABLE_AUDIT_LOG
    )?.value === 'true';

  // Validate the URL params.
  if (
    params.tab !== undefined &&
    !Object.values(ConfigTabKey).includes(params.tab as ConfigTabKey)
  ) {
    return <YBErrorIndicator customErrorMessage={t('error.pageNotFound')} />;
  }

  const defaultTab = isAvailable(currentCustomer.data.features, 'config.infra')
    ? ConfigTabKey.INFRA
    : ConfigTabKey.BACKUP;
  const activeTab = params.tab ?? defaultTab;
  const activeSection = params.section ?? 's3';
  return (
    <div>
      <h2 className="content-title">{t('title')}</h2>
      <RbacValidator accessRequiredOn={ApiPermissionMap.GET_PROVIDERS}>
        <YBTabsWithLinksPanel
          defaultTab={defaultTab}
          activeTab={activeTab}
          routePrefix={`/${CONFIG_ROUTE_PREFIX}/`}
          id="config-tab-panel"
          className="universe-detail data-center-config-tab"
        >
          {isAvailable(currentCustomer.data.features, 'config.infra') && (
            <Tab
              eventKey={ConfigTabKey.INFRA}
              title={t('tab.infrastructure.tabLabel')}
              key="infra-config"
            >
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
            <Tab eventKey="backup" title={t('tab.backup.tabLabel')} key="storage-config">
              <StorageConfigurationContainer
                activeTab={activeSection}
                routePrefix={CONFIG_ROUTE_PREFIX}
              />
            </Tab>
          )}

          {isExportLogEnabled && (
            <Tab eventKey={ConfigTabKey.LOG} title={t('tab.log.tabLabel')} key="log">
              <ExportLog />
            </Tab>
          )}

          {isAvailable(currentCustomer.data.features, 'config.security') && (
            <Tab
              eventKey={ConfigTabKey.SECURITY}
              title={t('tab.security.tabLabel')}
              key="security-config"
            >
              <SecurityConfiguration activeTab={params.section} />
            </Tab>
          )}
          {(featureFlags.test['enableMultiRegionConfig'] ||
            featureFlags.released['enableMultiRegionConfig']) && (
            <Tab
              eventKey={ConfigTabKey.BACKUP_NEW}
              title={t('tab.backupNew.tabLabel')}
              key="new-backup-config"
            >
              <NewStorageConfiguration activeTab={params.section} />
            </Tab>
          )}
          {isTroubleshootingEnabled && (
            <Tab
              eventKey={ConfigTabKey.TROUBLESHOOT}
              title={t('tab.troubleshoot.tabLabel')}
              key="troubleshoot-config"
            >
              <TroubleshootingDetails activeTab={params.section} />
            </Tab>
          )}
        </YBTabsWithLinksPanel>
      </RbacValidator>
    </div>
  );
};

const getTabTitle = (providerCode: ProviderCode | KubernetesProviderType) => {
  const I18N_KEY_PREFIX = 'integrations.tab.infrastructure.tab';
  switch (providerCode) {
    case ProviderCode.AWS:
      return (
        <div className="title">
          <img src={awsLogo} alt="AWS" className="aws-logo" />
          <span>{i18next.t(`${I18N_KEY_PREFIX}.aws`)}</span>
        </div>
      );
    case ProviderCode.GCP:
      return (
        <div className="title">
          <img src={gcpLogo} alt="GCP" className="gcp-logo" />
          <span>{i18next.t(`${I18N_KEY_PREFIX}.gcp`)}</span>
        </div>
      );
    case ProviderCode.AZU:
      return (
        <div className="title">
          <img src={azureLogo} alt="Azure" className="azure-logo" />
          <span>{i18next.t(`${I18N_KEY_PREFIX}.azu`)}</span>
        </div>
      );
    case KubernetesProviderType.TANZU:
      return (
        <div className="title">
          <img src={tanzuLogo} alt="VMware Tanzu" />
          <span>{i18next.t(`${I18N_KEY_PREFIX}.vmwareTanzu`)}</span>
        </div>
      );
    case KubernetesProviderType.OPEN_SHIFT:
      return (
        <div className="title">
          <img src={openshiftLogo} alt="Red Hat OpenShift" />
          <span>{i18next.t(`${I18N_KEY_PREFIX}.redHatOpenShift`)}</span>
        </div>
      );
    case KubernetesProviderType.MANAGED_SERVICE:
    case ProviderCode.KUBERNETES:
      return (
        <div className="title">
          <img src={k8sLogo} alt="Managed Kubernetes" />
          <span>{i18next.t(`${I18N_KEY_PREFIX}.managedK8s`)}</span>
        </div>
      );
    case ProviderCode.ON_PREM:
      return (
        <div className="title">
          <i className="fa fa-server tab-logo" />
          <span>{i18next.t(`${I18N_KEY_PREFIX}.onPrem`)}</span>
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
