import { FC, useEffect } from 'react';
import { Tab } from 'react-bootstrap';
import { browserHistory } from 'react-router';
import { useQuery } from 'react-query';
import { Selector, useSelector } from 'react-redux';
import { RouteComponentProps } from 'react-router-dom';

import { fetchGlobalRunTimeConfigs } from '../../api/admin';
import { YBTabsPanel, YBTabsWithLinksPanel } from '../panels';
import { isAvailable, showOrRedirect } from '../../utils/LayoutUtils';
import { HAReplication } from '../ha';
import { AlertConfigurationContainer } from '../alerts';
import { UserManagementContainer } from '../users';
import { RuntimeConfigContainer } from '../advanced';
import { HAInstancesContainer } from '../ha/instances/HAInstanceContainer';

import ListCACerts from '../customCACerts/ListCACerts';
import { RBACContainer } from '../../redesign/features/rbac/RBACContainer';
import { RbacValidator } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';
import { isRbacEnabled } from '../../redesign/features/rbac/common/RbacUtils';
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

interface FetureFlags {
  released: any;
  test: any;
}

interface FeatureStore {
  featureFlags: FetureFlags;
}

// string values will be used in URL
// eslint-disable-next-line @typescript-eslint/no-unused-vars
enum AdministrationTabs {
  HA = 'ha',
  AC = 'alertConfig'
}

// eslint-disable-next-line @typescript-eslint/no-unused-vars
enum HighAvailabilityTabs {
  Replication = 'replication',
  Instances = 'instances'
}

// eslint-disable-next-line @typescript-eslint/no-unused-vars
enum AlertConfigurationTabs {
  Creation = 'alertCreation'
}

const USER_MANAGAEMENT_TAB = {
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
  section: HighAvailabilityTabs | AlertConfigurationTabs;
}

const customerSelector: Selector<Store, Customer> = (state) => state.customer.currentCustomer;
const featureFlags: Selector<FeatureStore, FetureFlags> = (state) => state.featureFlags;

export const Administration: FC<RouteComponentProps<{}, RouteParams>> = ({ params }) => {
  const currentCustomer = useSelector(customerSelector);
  const { test, released } = useSelector(featureFlags);
  const globalRuntimeConfigs = useQuery(['globalRuntimeConfigs'], () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );

  const isCongifUIEnabled =
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
      <Tab eventKey="alertConfig" title="Alert Configurations" key="alert-configurations">
        <AlertConfigurationContainer
          defaultTab={AlertConfigurationTabs.Creation}
          activeTab={params.section}
          routePrefix={`/admin/${AdministrationTabs.AC}/`}
        />
      </Tab>
    ) : null;
  };

  const getHighAvailabilityTab = () => {
    return isAvailable(currentCustomer.data.features, 'administration.highAvailability') ? (
      <Tab eventKey="ha" title="High Availability" key="high-availability">
        <RbacValidator accessRequiredOn={ApiPermissionMap.GET_HA_CONFIG}>
          <YBTabsPanel
            defaultTab={HighAvailabilityTabs.Replication}
            activeTab={params.section}
            routePrefix={`/admin/${AdministrationTabs.HA}/`}
            id="administration-ha-subtab"
            className="config-tabs"
          >
            <Tab
              eventKey={HighAvailabilityTabs.Replication}
              title={
                <span>
                  <i className="fa fa-clone tab-logo" aria-hidden="true"></i> Replication
                  Configuration
                </span>
              }
              unmountOnExit
            >
              <HAReplication />
            </Tab>
            <Tab
              eventKey={HighAvailabilityTabs.Instances}
              title={
                <span>
                  <i className="fa fa-codepen tab-logo" aria-hidden="true"></i> Instance
                  Configuration
                </span>
              }
              unmountOnExit
            >
              <HAInstancesContainer />
            </Tab>
          </YBTabsPanel>
        </RbacValidator>
      </Tab>
    ) : null;
  };

  const getUserManagementTab = () => {
    const { id, title, defaultTab } = USER_MANAGAEMENT_TAB;
    return (
      <Tab eventKey={id} title={title} key={id}>
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
      <Tab eventKey={id} title={title} key={id}>
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
      <Tab eventKey="custom-ca-certs" title="CA Certificates" key="CA_Certificates">
        <ListCACerts />
      </Tab>
    );
  };

  const getRbacTab = () => {
    return (
      <Tab eventKey="rbac" title="Access Management" key="rbac">
        <RBACContainer />
      </Tab>
    );
  };

  return (
    <div>
      <h2 className="content-title">Platform Configuration</h2>
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
        {isCongifUIEnabled && getAdvancedTab()}
        {isRbacEnabled() && getRbacTab()}
      </YBTabsWithLinksPanel>
    </div>
  );
};
