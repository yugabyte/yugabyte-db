// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold all the alert configuration tabs along
// with their respective components.

import { useEffect, useState } from 'react';
import { Tab } from 'react-bootstrap';
import { isDisabled } from '../../../utils/LayoutUtils';
import { YBTabsPanel } from '../../panels';
import { AlertDestinations } from './AlertDestinations';
import { AlertProfileForm } from '../../profile';
import AlertDestinationConfiguration from './AlertDestinationConfiguration';
import { AlertsList } from './AlertsList';
import CreateAlert from './CreateAlert';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { AlertDestinationChannels } from './AlertDestinationChannels';
import { MaintenanceWindow } from '../MaintenanceWindow';

export const AlertConfiguration = (props) => {
  const [listView, setListView] = useState(false);
  const [targetMetrics, setTargetMetrics] = useState([]);
  const [alertUniverseList, setAlertUniverseList] = useState([]);
  const [enablePlatformAlert, setPlatformAlert] = useState(false);
  const [alertDestinationListView, setAlertDestinationListView] = useState(false);

  const {
    activeTab,
    apiToken,
    customer,
    customerProfile,
    defaultTab,
    getTargetMetrics,
    routePrefix,
    universes
  } = props;

  const onInit = () => {
    if (!getPromiseState(universes).isSuccess()) {
      props.fetchUniverseList().then((data) => {
        setAlertUniverseList([
          ...data?.map((universe) => ({
            label: universe.name,
            value: universe.universeUUID
          })) ?? []
        ]);
      });
    }
    // if universe list is already fetched, load it from the store
    else {
      setAlertUniverseList([
        ...props.universes.data.map((universe) => ({
          label: universe.name,
          value: universe.universeUUID
        }))
      ]);
    }
  };

  useEffect(onInit, []);

  /**
   * This method is used to handle the metrics API call based on
   * the target type which will be UNIVERSE or CUSTOMER.
   *
   * @param {string} targetType
   */
  const handleMetricsCall = (targetType) => {
    getTargetMetrics(targetType).then((response) => {
      setTargetMetrics(response);
    });
  };

  return (
    <div className="provider-config-container">
      <YBTabsPanel
        activeTab={activeTab}
        className="config-tabs"
        defaultTab={defaultTab}
        id="alert-config-tab-panel"
        routePrefix={routePrefix}
      >
        {/* Alert Creation Tab */}
        <Tab
          eventKey={defaultTab}
          title={
            <span>
              <i className="fa fa-bell-o tab-logo" aria-hidden="true"></i> Alert Policies
            </span>
          }
          unmountOnExit
        >
          {listView ? (
            <CreateAlert
              onCreateCancel={setListView}
              enablePlatformAlert={enablePlatformAlert}
              metricsData={targetMetrics}
              alertUniverseList={alertUniverseList}
              {...props}
            />
          ) : (
            <AlertsList
              onCreateAlert={setListView}
              enablePlatformAlert={setPlatformAlert}
              handleMetricsCall={handleMetricsCall}
              alertUniverseList={alertUniverseList}
              universes={universes}
              {...props}
            />
          )}
        </Tab>

        {/* Alert Destination Tab */}
        <Tab
          eventKey="alertDestinations"
          title={
            <span>
              <i className="fa fa-compass tab-logo" aria-hidden="true"></i> Alert Destinations
            </span>
          }
          unmountOnExit
        >
          {alertDestinationListView ? (
            <AlertDestinationConfiguration onAddCancel={setAlertDestinationListView} {...props} />
          ) : (
            <AlertDestinations onAddAlertDestination={setAlertDestinationListView} {...props} />
          )}
        </Tab>
        <Tab
          eventKey="notificationChannels"
          title={
            <span>
              <i className="fa fa-exchange tab-logo" aria-hidden="true"></i> Notification Channels
            </span>
          }
          unmountOnExit
        >
          <AlertDestinationChannels {...props} />
        </Tab>
        {/* Helath Check Tab */}
        <Tab
          eventKey="health-alerting"
          title={
            <span>
              <i className="fa fa-heartbeat tab-logo" aria-hidden="true"></i> Health
            </span>
          }
          key="health-alerting-tab"
          mountOnEnter={true}
          unmountOnExit
          disabled={isDisabled(customer.data.features, 'main.profile')}
        >
          <AlertProfileForm
            customer={customer}
            customerProfile={customerProfile}
            apiToken={apiToken}
            {...props}
          />
        </Tab>
        <Tab
          eventKey="maintenanceWindow"
          title={
            <span>
              <i className="fa fa-cog tab-logo" aria-hidden="true"></i> Maintenance Windows
            </span>
          }
          unmountOnExit
        >
          <MaintenanceWindow />
        </Tab>
      </YBTabsPanel>
    </div>
  );
};
