import React, { FC } from 'react';
import { Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

import { YBTabsPanel } from '../../panels';
import { GlobalRuntimeConfig } from './GlobalRuntimeConfig';
import { CustomerRuntimeConfig } from './CustomerRuntimeConfig';
import { UniverseRuntimeConfig } from './UniverseRuntimeConfig';
import { ProviderRuntimeConfig } from './ProviderRuntimeConfig';

import '../AdvancedConfig.scss';

interface RuntimeConfigProps {
  activeTab: string;
  defaultTab: string;
  routePrefix: string;
  fetchRuntimeConfigs: () => void;
  setRuntimeConfig: (key: string, value: string, scope?: string) => void;
  deleteRunTimeConfig: (key: string, scope?: string) => void;
  resetRuntimeConfigs: () => void;
}

export const RuntimeConfig: FC<RuntimeConfigProps> = ({
  activeTab,
  defaultTab,
  routePrefix,
  fetchRuntimeConfigs,
  setRuntimeConfig,
  deleteRunTimeConfig,
  resetRuntimeConfigs
}) => {
  const { t } = useTranslation();

  return (
    <div>
      <YBTabsPanel
        activeTab={activeTab}
        defaultTab={defaultTab}
        routePrefix={routePrefix}
        id="global-config-tab-panel"
        className="config-tabs"
      >
        <Tab
          eventKey="global-config"
          title={t('admin.advanced.globalConfig.GlobalConfigTitle')}
          unmountOnExit
        >
          <GlobalRuntimeConfig
            setRuntimeConfig={setRuntimeConfig}
            deleteRunTimeConfig={deleteRunTimeConfig}
            fetchRuntimeConfigs={fetchRuntimeConfigs}
            resetRuntimeConfigs={resetRuntimeConfigs}
          />
        </Tab>

        <Tab
          eventKey="customer-config"
          title={t('admin.advanced.globalConfig.CustomerConfigTitle')}
          unmountOnExit
        >
          <CustomerRuntimeConfig
            setRuntimeConfig={setRuntimeConfig}
            deleteRunTimeConfig={deleteRunTimeConfig}
            fetchRuntimeConfigs={fetchRuntimeConfigs}
            resetRuntimeConfigs={resetRuntimeConfigs}
          />
        </Tab>

        <Tab
          eventKey="universe-config"
          title={t('admin.advanced.globalConfig.UniverseConfigTitle')}
          unmountOnExit
        >
          <UniverseRuntimeConfig
            setRuntimeConfig={setRuntimeConfig}
            deleteRunTimeConfig={deleteRunTimeConfig}
            fetchRuntimeConfigs={fetchRuntimeConfigs}
            resetRuntimeConfigs={resetRuntimeConfigs}
          />
        </Tab>

        <Tab
          eventKey="provider-config"
          title={t('admin.advanced.globalConfig.ProviderConfigTitle')}
          unmountOnExit
        >

          <ProviderRuntimeConfig
            setRuntimeConfig={setRuntimeConfig}
            deleteRunTimeConfig={deleteRunTimeConfig}
            fetchRuntimeConfigs={fetchRuntimeConfigs}
            resetRuntimeConfigs={resetRuntimeConfigs}
          />
        </Tab>
      </YBTabsPanel>
    </div>
  );
}
