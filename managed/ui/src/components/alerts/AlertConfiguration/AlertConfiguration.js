// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//

import React, { useEffect, useState } from 'react';
import { Tab } from 'react-bootstrap';
import { YBTabsPanel } from '../../panels';
import { AlertDestionations } from './AlertDestinations';
import { AlertsList } from './AlertsList';
import CreateAlert from './CreateAlert';

export const AlertConfiguration = (props) => {
  const [alertList, setAlertList] = useState([]);
  const [alertDestionation, setAlertDesionation] = useState([]);
  const [listView, setListView] = useState(false);
  const { activeTab, defaultTab, routePrefix } = props;

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
              <i className="fa fa-clone tab-logo" aria-hidden="true"></i> Alert Creation
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
      </YBTabsPanel>
    </div>
  );
};
