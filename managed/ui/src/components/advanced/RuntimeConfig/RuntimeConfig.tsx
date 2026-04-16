import { FC } from 'react';
import { Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

import { YBTabsPanel } from '../../panels';
import { GlobalRuntimeConfig } from './GlobalRuntimeConfig';
import { CustomerRuntimeConfig } from './CustomerRuntimeConfig';
import { UniverseRuntimeConfig } from './UniverseRuntimeConfig';
import { ProviderRuntimeConfig } from './ProviderRuntimeConfig';

import { Action, Resource } from '../../../redesign/features/rbac';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { userhavePermInRoleBindings } from '../../../redesign/features/rbac/common/RbacUtils';
import '../AdvancedConfig.scss';

interface RuntimeConfigProps {
  activeTab: string;
  defaultTab: string;
  routePrefix: string;
  getRuntimeConfig: (key: string, scope?: string) => void;
  fetchRuntimeConfigs: () => void;
  setRuntimeConfig: (key: string, value: string, scope?: string) => void;
  deleteRunTimeConfig: (key: string, scope?: string) => void;
  resetRuntimeConfigs: () => void;
}

export const RuntimeConfig: FC<RuntimeConfigProps> = ({
  activeTab,
  defaultTab,
  routePrefix,
  getRuntimeConfig,
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
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.GET_RUNTIME_CONFIG_BY_SCOPE}
            overrideStyle={{ marginTop: '150px' }}
          >
            <GlobalRuntimeConfig
              getRuntimeConfig={getRuntimeConfig}
              setRuntimeConfig={setRuntimeConfig}
              deleteRunTimeConfig={deleteRunTimeConfig}
              fetchRuntimeConfigs={fetchRuntimeConfigs}
              resetRuntimeConfigs={resetRuntimeConfigs}
            />
          </RbacValidator>
        </Tab>

        <Tab
          eventKey="customer-config"
          title={t('admin.advanced.globalConfig.CustomerConfigTitle')}
          unmountOnExit
        >
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.GET_RUNTIME_CONFIG_BY_SCOPE}
            overrideStyle={{ marginTop: '50px' }}
          >
            <CustomerRuntimeConfig
              getRuntimeConfig={getRuntimeConfig}
              setRuntimeConfig={setRuntimeConfig}
              deleteRunTimeConfig={deleteRunTimeConfig}
              fetchRuntimeConfigs={fetchRuntimeConfigs}
              resetRuntimeConfigs={resetRuntimeConfigs}
            />
          </RbacValidator>
        </Tab>

        <Tab
          eventKey="universe-config"
          title={t('admin.advanced.globalConfig.UniverseConfigTitle')}
          unmountOnExit
        >
          <RbacValidator
            customValidateFunction={() => {
              return userhavePermInRoleBindings(Resource.UNIVERSE, Action.READ);
            }}
            overrideStyle={{ marginTop: '150px' }}
          >
            <UniverseRuntimeConfig
              getRuntimeConfig={getRuntimeConfig}
              setRuntimeConfig={setRuntimeConfig}
              deleteRunTimeConfig={deleteRunTimeConfig}
              fetchRuntimeConfigs={fetchRuntimeConfigs}
              resetRuntimeConfigs={resetRuntimeConfigs}
            />
          </RbacValidator>
        </Tab>

        <Tab
          eventKey="provider-config"
          title={t('admin.advanced.globalConfig.ProviderConfigTitle')}
          unmountOnExit
        >
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.GET_RUNTIME_CONFIG_BY_SCOPE}
            overrideStyle={{ marginTop: '150px' }}
          >
            <ProviderRuntimeConfig
              getRuntimeConfig={getRuntimeConfig}
              setRuntimeConfig={setRuntimeConfig}
              deleteRunTimeConfig={deleteRunTimeConfig}
              fetchRuntimeConfigs={fetchRuntimeConfigs}
              resetRuntimeConfigs={resetRuntimeConfigs}
            />
          </RbacValidator>
        </Tab>
      </YBTabsPanel>
    </div>
  );
};
