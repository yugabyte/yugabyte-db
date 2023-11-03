// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import 'react-fa';
import { MenuItem, NavDropdown, Navbar, Nav } from 'react-bootstrap';
import { Link } from 'react-router';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { LinkContainer } from 'react-router-bootstrap';
import { isNotHidden, isDisabled } from '../../../utils/LayoutUtils';
import { clearCredentials } from '../../../routes';
import { isRbacEnabled } from '../../../redesign/features/rbac/common/RbacUtils';
import { hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacValidator';
import { Action, Resource } from '../../../redesign/features/rbac';
import './stylesheets/TopNavBar.scss';
import YBLogo from '../YBLogo/YBLogo';

class YBMenuItem extends Component {
  render() {
    const { disabled, to, id, className, onClick } = this.props;
    if (disabled)
      return (
        <li>
          <div className={className}>{this.props.children}</div>
        </li>
      );

    return (
      <LinkContainer to={to} id={id}>
        <MenuItem className={className} onClick={onClick}>
          {this.props.children}
        </MenuItem>
      </LinkContainer>
    );
  }
}

export default class TopNavBar extends Component {
  handleLogout = (event) => {
    clearCredentials();
    this.props.logoutProfile();
  };

  render() {
    const {
      customer: { currentCustomer }
    } = this.props;
    const customerEmail = getPromiseState(currentCustomer).isSuccess()
      ? currentCustomer.data.email
      : '';

    const hasDefaultReadPerm =  hasNecessaryPerm({
      permissionRequired: [Action.READ],
      resourceType: Resource.DEFAULT
    });

    // TODO(bogdan): icon for logs...
    return (
      <Navbar fixedTop>
        {getPromiseState(currentCustomer).isSuccess() &&
          isNotHidden(currentCustomer.data.features, 'menu.sidebar') && (
            <Navbar.Header>
              <Link to="/" className="left_col text-center">
                <YBLogo />
              </Link>
            </Navbar.Header>
          )}
        <div className="flex-grow"></div>
        {(isRbacEnabled() || getPromiseState(currentCustomer).isSuccess()) &&
          isNotHidden(currentCustomer.data.features, 'main.dropdown') && (
            <Nav pullRight>
              <NavDropdown
                eventKey="2"
                title={
                  <span>
                    <i className="fa fa-user fa-fw"></i> {customerEmail}
                  </span>
                }
                id="profile-dropdown"
              >
                {isNotHidden(currentCustomer.data.features, 'main.profile') && (
                  <YBMenuItem
                    to={'/profile'}
                    disabled={isDisabled(currentCustomer.data.features, 'main.profile')}
                  >
                    <i className="fa fa-user fa-fw"></i>User Profile
                  </YBMenuItem>
                )}
                {isNotHidden(currentCustomer.data.features, 'main.logs') && hasDefaultReadPerm && (
                  <YBMenuItem
                    to={'/logs'}
                    disabled={isDisabled(currentCustomer.data.features, 'main.logs')}
                  >
                    <i className="fa fa-file fa-fw"></i>Logs
                  </YBMenuItem>
                )}
                {isNotHidden(currentCustomer.data.features, 'main.releases') && hasDefaultReadPerm && (
                  <YBMenuItem
                    to={'/releases'}
                    disabled={isDisabled(currentCustomer.data.features, 'main.releases')}
                  >
                    <i className="fa fa-code-fork fa-fw"></i>Releases
                  </YBMenuItem>
                )}
                <YBMenuItem to="/login" id="logoutLink" onClick={this.handleLogout}>
                  <i className="fa fa-sign-out fa-fw"></i>Logout
                </YBMenuItem>
              </NavDropdown>
            </Nav>
          )}
      </Navbar>
    );
  }
}
