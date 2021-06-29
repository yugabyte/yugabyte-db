// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//

import React, { useEffect, useState } from 'react';
import { Tab } from 'react-bootstrap';
import { isDisabled } from '../../../utils/LayoutUtils';
import { YBTabsPanel } from '../../panels';
import { AlertDestionations } from './AlertDestinations';
import { AlertProfileForm } from '../../profile';
import AlertDestinationConfiguration from './AlertDestinationConfiguration';
import { AlertsList } from './AlertsList';
import CreateAlert from './CreateAlert';

export const AlertConfiguration = (props) => {
  const [alertList, setAlertList] = useState([]);
  const [alertDestionation, setAlertDesionation] = useState([]);
  const [profileStatus, setProfileStatus] = useState({
    statusUpdated: true,
    updateStatus: ''
  });
  const [listView, setListView] = useState(false);
  const [alertDestinationListView, setAlertDestinationListView] = useState(true);
  const { activeTab, defaultTab, routePrefix, customerProfile, apiToken, customer } = props;

  const handleProfileUpdate = (status) => {
    setProfileStatus({
      statusUpdated: false,
      updateStatus: status
    });
  };

  useEffect(() => {
    setAlertList(props.alertConfigs());
    setAlertDesionation(props.alertDestionations());
  }, []);

  return (
    <div className="provider-config-container">
      <YBTabsPanel
        activeTab={activeTab}
        className="config-tabs"
        defaultTab={defaultTab}
        id="alert-config-tab-panel"
        routePrefix={routePrefix}
      >
        <Tab
          eventKey={defaultTab}
          title={
            <span>
              <i className="fa fa-bell-o tab-logo" aria-hidden="true"></i> Alert Creation
            </span>
          }
          unmountOnExit
        >
          {listView ? (
            <CreateAlert onCreateCancel={setListView} />
          ) : (
            <AlertsList data={alertList} onCreateAlert={setListView} />
          )}
        </Tab>
        <Tab
          eventKey={'health-alerting'}
          eventKey="alertDestinations"
          title={
            <span>
              <i className="fa fa-clone tab-logo" aria-hidden="true"></i> Alert Destinations
            </span>
          }
          unmountOnExit
        >
          <AlertDestionations data={alertDestionation} />
        </Tab>
        <Tab
          eventKey="health-alerting"
          title="Health & Alerting"
          key="health-alerting-tab"
          mountOnEnter={true}
          unmountOnExit
          disabled={isDisabled(customer.data.features, 'main.profile')}
        >
          <AlertProfileForm
            customer={customer}
            customerProfile={customerProfile}
            apiToken={apiToken}
            handleProfileUpdate={handleProfileUpdate}
            {...props}
          />
        </Tab>
      </YBTabsPanel>
    </div>
  );
};
