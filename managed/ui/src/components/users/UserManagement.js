import React from 'react';
import { Tab } from 'react-bootstrap';
import { YBTabsPanel } from '../panels';
import UsersListContainer from './Users/UsersListContainer';
import './styles.scss';

export const UserManagement = (props) => {
  const { activeTab, defaultTab, routePrefix } = props;

  return (
    <div>
      <YBTabsPanel
        activeTab={activeTab}
        defaultTab={defaultTab}
        routePrefix={routePrefix}
        id="user-management-tab-panel"
        className="config-tabs"
      >
        <Tab
          eventKey="users"
          title={
            <span>
              <i className="fa fa-user user-tab-logo" aria-hidden="true"></i> Users
            </span>
          }
          unmountOnExit
        >
          <UsersListContainer />
        </Tab>
      </YBTabsPanel>
    </div>
  );
};
