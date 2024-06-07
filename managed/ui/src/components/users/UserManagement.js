import { Tab, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { YBTabsPanel } from '../panels';
import UsersListContainer from './Users/UsersListContainer';
import UserAuthContainer from './UserAuth/UserAuthContainer';
import { isRbacEnabled } from '../../redesign/features/rbac/common/RbacUtils';
import { YBLoading } from '../common/indicators';
import KeyIcon from './icons/key_icon';
import './styles.scss';

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
            {"You don't have enough permission"}
          </Tooltip>
        }
      >
        {AuthTab()}
      </OverlayTrigger>
    );
  };

  const havePermission = () => {
    if (isRbacEnabled()) return true;
    return isAdmin || isLoading;
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
          disabled={!havePermission()}
          eventKey="user-auth"
          title={havePermission() ? <AuthTab /> : <AuthTabWithOverlay />}
          unmountOnExit
        >
          {isLoading ? <YBLoading /> : <UserAuthContainer isAdmin={isAdmin} />}
        </Tab>
      </YBTabsPanel>
    </div>
  );
};
