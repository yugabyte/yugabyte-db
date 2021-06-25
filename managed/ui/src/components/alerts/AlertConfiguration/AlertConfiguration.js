// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//

import React from 'react';
import { Tab } from 'react-bootstrap';
import { YBTabsPanel } from '../../panels';
import { AlertsList } from './AlertsList';
import mockData from './MockData.json';

export const AlertConfiguration = (props) => {
  const { activeTab, defaultTab, routePrefix } = props;

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
          <AlertsList data={mockData} />
        </Tab>
      </YBTabsPanel>
    </div>
  );
};
