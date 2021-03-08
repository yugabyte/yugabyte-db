import React, { FC, useEffect } from 'react';
import { Tab } from 'react-bootstrap';
import { browserHistory } from 'react-router';
import { Selector, useSelector } from 'react-redux';
import { RouteComponentProps } from 'react-router-dom';
import { YBTabsPanel } from '../components/panels';
import { showOrRedirect } from '../utils/LayoutUtils';
import { HAInstances, HAReplication } from '../components/ha';
import './Administration.scss';

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

// string values will be used in URL
enum AdministrationTabs {
  HA = 'ha'
}
enum HighAvailabilityTabs {
  Replication = 'replication',
  Instances = 'instances'
}

interface RouteParams {
  tab: AdministrationTabs;
  section: HighAvailabilityTabs;
}

const DEFAULT_ADMIN_PAGE = `/admin/${AdministrationTabs.HA}/${HighAvailabilityTabs.Replication}`;
const customerSelector: Selector<Store, Customer> = (state) => state.customer.currentCustomer;

export const Administration: FC<RouteComponentProps<{}, RouteParams>> = ({ params }) => {
  const currentCustomer = useSelector(customerSelector);

  useEffect(() => {
    showOrRedirect(currentCustomer.data.features, 'menu.administration');
    if (!params.tab || !params.section) {
      browserHistory.replace(DEFAULT_ADMIN_PAGE);
    }
  }, [currentCustomer, params.tab, params.section]);

  return (
    <div className="dashboard-container">
      <h2 className="content-title">Platform Configuration</h2>
      <YBTabsPanel
        defaultTab={AdministrationTabs.HA}
        activeTab={params.tab}
        routePrefix="/admin/"
        id="administration-main-tabs"
      >
        <Tab eventKey={AdministrationTabs.HA} title={<span className="beta">High Availability</span>}>
          <YBTabsPanel
            defaultTab={HighAvailabilityTabs.Replication}
            activeTab={params.section}
            routePrefix={`/admin/${AdministrationTabs.HA}/`}
            id="administration-ha-subtab"
          >
            <Tab
              eventKey={HighAvailabilityTabs.Replication}
              title="Replication Configuration"
              unmountOnExit
            >
              <HAReplication />
            </Tab>
            <Tab
              eventKey={HighAvailabilityTabs.Instances}
              title="Instance Configuration"
              unmountOnExit
            >
              <HAInstances />
            </Tab>
          </YBTabsPanel>
        </Tab>
      </YBTabsPanel>
    </div>
  );
};
