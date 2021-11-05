import React, { FC, useEffect } from 'react';
import { Tab } from 'react-bootstrap';
import { browserHistory } from 'react-router';
import { Selector, useSelector } from 'react-redux';
import { RouteComponentProps } from 'react-router-dom';
import { YBTabsPanel, YBTabsWithLinksPanel } from '../components/panels';
import { isAvailable, showOrRedirect } from '../utils/LayoutUtils';
import { HAInstances, HAReplication } from '../components/ha';
import './Administration.scss';
import { AlertConfigurationContainer } from '../components/alerts';

// very basic redux store definition, just enough to compile without ts errors
interface Store {
  customer: {
    currentCustomer: Customer;
  };
}

interface Customer {
  data: {
    features?: Record<string, any>;
  };
}

interface FeatureStore {
  featureFlags: FetureFlags;
}

interface FetureFlags {
  released: any;
  test: any;
}

// string values will be used in URL
enum AdministrationTabs {
  HA = 'ha',
  AC = 'alertConfig'
}
enum HighAvailabilityTabs {
  Replication = 'replication',
  Instances = 'instances'
}

enum AlertConfigurationTabs {
  Creation = 'alertCreation'
}

interface RouteParams {
  tab: AdministrationTabs;
  section: HighAvailabilityTabs | AlertConfigurationTabs;
}

const customerSelector: Selector<Store, Customer> = (state) => state.customer.currentCustomer;
const featureFlags: Selector<FeatureStore, FetureFlags> = (state) => state.featureFlags;

export const Administration: FC<RouteComponentProps<{}, RouteParams>> = ({ params }) => {
  const currentCustomer = useSelector(customerSelector);
  const { test, released } = useSelector(featureFlags);
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
            <HAInstances />
          </Tab>
        </YBTabsPanel>
      </Tab>
    ) : null;
  }

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
      </YBTabsWithLinksPanel>
    </div>
  );
};
