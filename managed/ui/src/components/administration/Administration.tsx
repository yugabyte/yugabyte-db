import { FC, useEffect } from 'react';
import { Tab } from 'react-bootstrap';
import { browserHistory } from 'react-router';
import { useQuery } from 'react-query';
import { Selector, useSelector } from 'react-redux';
import { RouteComponentProps } from 'react-router-dom';

import { fetchGlobalRunTimeConfigs } from '../../api/admin';
import { YBTabsPanel, YBTabsWithLinksPanel } from '../panels';
import { isAvailable, showOrRedirect } from '../../utils/LayoutUtils';
import { AlertConfigurationContainer } from '../alerts';
import { UserManagementContainer } from '../users';
import { RuntimeConfigContainer } from '../advanced';
import ListCACerts from '../customCACerts/ListCACerts';
import { RBACContainer } from '../../redesign/features/rbac/RBACContainer';
import { RbacValidator } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';
import { isRbacEnabled } from '../../redesign/features/rbac/common/RbacUtils';
import { ContinuousBackup } from '../../redesign/features/continuous-backup/ContinuousBackup';
import { PlatformHa } from '../../redesign/features/platform-ha/PlatformHa';

import './Administration.scss';

// very basic redux store definition, just enough to compile without ts errors
interface Customer {
  data: {
    features?: Record<string, any>;
  };
}

interface Store {
  customer: {
    currentCustomer: Customer;
  };
}

interface FeatureFlags {
  released: any;
  test: any;
}

interface FeatureStore {
  featureFlags: FeatureFlags;
}

const AdministrationTabs = {
  HA: 'ha',
  AC: 'alertConfig'
} as const;
type AdministrationTabs = typeof AdministrationTabs[keyof typeof AdministrationTabs];

const PlatformHaAndBackupsTabs = {
  PLATFORM_HA: 'platformHa',
  CONTINUOUS_BACKUP: 'continuousBackup'
} as const;
type PlatformHaAndBackupsTabs = typeof PlatformHaAndBackupsTabs[keyof typeof PlatformHaAndBackupsTabs];

const AlertConfigurationTabs = {
  Creation: 'alertCreation'
} as const;
type AlertConfigurationTabs = typeof AlertConfigurationTabs[keyof typeof AlertConfigurationTabs];

const USER_MANAGEMENT_TAB = {
  title: 'User Management',
  id: 'user-management',
  defaultTab: 'users'
};

const ADVANCED_TAB = {
  title: 'Advanced',
  id: 'advanced',
  defaultTab: 'global-config'
};

interface RouteParams {
  tab: AdministrationTabs;
  section: PlatformHaAndBackupsTabs | AlertConfigurationTabs;
}

const customerSelector: Selector<Store, Customer> = (state) => state.customer.currentCustomer;
const featureFlags: Selector<FeatureStore, FeatureFlags> = (state) => state.featureFlags;

export const Administration: FC<RouteComponentProps<{}, RouteParams>> = ({ params }) => {
  const currentCustomer = useSelector(customerSelector);
  const { test, released } = useSelector(featureFlags);
  const globalRuntimeConfigs = useQuery(['globalRuntimeConfigs'], () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );

  const isRuntimeConfigUiEnabled =
    globalRuntimeConfigs?.data?.configEntries?.find(
      (c: any) => c.key === 'yb.runtime_conf_ui.enable_for_all'
    )?.value === 'true' ||
    test['enableRunTimeConfig'] ||
    released['enableRunTimeConfig'];

  const defaultTab = isAvailable(currentCustomer.data.features, 'administration.highAvailability')
    ? AdministrationTabs.HA
    : AdministrationTabs.AC;

  useEffect(() => {
    showOrRedirect(currentCustomer.data.features, 'menu.administration');
    // redirect to a proper url on navigation from sidebar "Admin" button (the "/admin" route)
    if (!params.tab && !params.section) {
      const url = `/admin/${defaultTab}`;
      browserHistory.replace(url);
    }
  }, [currentCustomer, defaultTab, params.tab, params.section]);

  const getAlertTab = () => {
    return test?.adminAlertsConfig || released?.adminAlertsConfig ? (
      <Tab
        eventKey="alertConfig"
        title="Alert Configurations"
        key="alert-configurations"
        unmountOnExit
      >
        <AlertConfigurationContainer
          defaultTab={AlertConfigurationTabs.Creation}
          activeTab={params.section}
          routePrefix={`/admin/${AdministrationTabs.AC}/`}
        />
      </Tab>
    ) : null;
  };

  const isContinuousBackupsUiEnable =
    globalRuntimeConfigs?.data?.configEntries?.find(
      (runtimeConfig: any) =>
        runtimeConfig.key === 'yb.ui.feature_flags.continuous_platform_backups'
    )?.value === 'true';
  const getHighAvailabilityTab = () => {
    return isAvailable(currentCustomer.data.features, 'administration.highAvailability') ? (
      <Tab eventKey="ha" title="Platform HA and Backups" key="high-availability" unmountOnExit>
        <RbacValidator accessRequiredOn={ApiPermissionMap.GET_HA_CONFIG}>
          <YBTabsPanel
            defaultTab={PlatformHaAndBackupsTabs.PLATFORM_HA}
            activeTab={params.section}
            routePrefix={`/admin/${AdministrationTabs.HA}/`}
            id="administration-ha-subtab"
            className="config-tabs"
          >
            <Tab
              eventKey={PlatformHaAndBackupsTabs.PLATFORM_HA}
              title="Platform High Availability"
              unmountOnExit
            >
              <PlatformHa />
            </Tab>
            {isContinuousBackupsUiEnable && (
              <Tab
                eventKey={PlatformHaAndBackupsTabs.CONTINUOUS_BACKUP}
                title="Automated Platform Backups"
                unmountOnExit
              >
                <ContinuousBackup />
              </Tab>
            )}
          </YBTabsPanel>
        </RbacValidator>
      </Tab>
    ) : null;
  };

  const getUserManagementTab = () => {
    const { id, title, defaultTab } = USER_MANAGEMENT_TAB;
    return (
      <Tab eventKey={id} title={title} key={id} unmountOnExit>
        <UserManagementContainer
          defaultTab={defaultTab}
          activeTab={params.section}
          routePrefix={`/admin/${id}/`}
        />
      </Tab>
    );
  };

  const getAdvancedTab = () => {
    const { id, title, defaultTab } = ADVANCED_TAB;
    return (
      <Tab eventKey={id} title={title} key={id} unmountOnExit>
        <RuntimeConfigContainer
          defaultTab={defaultTab}
          activeTab={params.section}
          routePrefix={`/admin/${id}/`}
        />
      </Tab>
    );
  };

  const getCustomCACertsTab = () => {
    return (
      <Tab eventKey="custom-ca-certs" title="CA Certificates" key="CA_Certificates" unmountOnExit>
        <ListCACerts />
      </Tab>
    );
  };

  const getRbacTab = () => {
    return (
      <Tab eventKey="rbac" title="Access Management" key="rbac" unmountOnExit>
        <RBACContainer />
      </Tab>
    );
  };

  return (
    <div>
      <h2 className="content-title">Platform Administration</h2>
      <YBTabsWithLinksPanel
        defaultTab={defaultTab}
        activeTab={params.tab}
        routePrefix="/admin/"
        id="administration-main-tabs"
        className="universe-detail data-center-config-tab"
      >
        {getHighAvailabilityTab()}
        {getAlertTab()}
        {!isRbacEnabled() && getUserManagementTab()}
        {getCustomCACertsTab()}
        {isRuntimeConfigUiEnabled && getAdvancedTab()}
        {isRbacEnabled() && getRbacTab()}
      </YBTabsWithLinksPanel>
    </div>
  );
};
