import React from 'react';
import { Tab, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { YBTabsPanel } from '../panels';
import UsersListContainer from './Users/UsersListContainer';
import UserAuthContainer from './UserAuth/UserAuthContainer';
import KeyIcon from './icons/key_icon';
import './styles.scss';
import { YBLoading } from '../common/indicators';

export const UserManagement = (props) => {
  const { activeTab, defaultTab, routePrefix, currentUserInfo } = props;
  const role = currentUserInfo?.role;
  const isAdmin = ['SuperAdmin'].includes(role);
  const isLoading = !role;

  const AuthTab = () => {
    return (
      <span>
        <KeyIcon />
        User Authentication
      </span>
    );
  };

  const AuthTabWithOverlay = () => {
    return (
      <OverlayTrigger
        placement="right"
        overlay={
          <Tooltip className="high-index" id="user-auth-tooltip">
            You don't have enough permission
          </Tooltip>
        }
      >
        {AuthTab()}
      </OverlayTrigger>
    );
  };

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
        <Tab
          disabled={!isAdmin || isLoading}
          eventKey="user-auth"
          title={isLoading || isAdmin ? <AuthTab /> : <AuthTabWithOverlay />}
          unmountOnExit
        >
          {isLoading ? <YBLoading /> : <UserAuthContainer isAdmin={isAdmin} />}
        </Tab>
      </YBTabsPanel>
    </div>
  );
};
