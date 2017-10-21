// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import 'react-fa';
import { MenuItem , NavDropdown, Navbar, Nav, NavItem } from 'react-bootstrap';
import { Link } from 'react-router';
import { HighlightedStatsPanelContainer } from '../../panels';
import { TaskAlertsContainer } from '../../tasks';
import YBLogo from '../YBLogo/YBLogo';
import './stylesheets/TopNavBar.scss';
import { getPromiseState } from 'utils/PromiseUtils';
import {LinkContainer} from 'react-router-bootstrap';

export default class TopNavBar extends Component {
  constructor(props) {
    super(props);
    this.handleLogout = this.handleLogout.bind(this);
  }

  handleLogout(event) {
    localStorage.clear();
    this.props.logoutProfile();
  }

  render() {
    const { customer: { yugawareVersion, currentCustomer } } = this.props;
    const version = getPromiseState(yugawareVersion).isSuccess()
        ? yugawareVersion.data.version
        : null;
    const customerEmail = getPromiseState(currentCustomer).isSuccess()
        ? currentCustomer.data.email
        : "";

    return (
      <Navbar fixedTop>
        <Navbar.Header>
          <Link to="/" className="col-md-3 left_col text-center">
            <YBLogo size="icon"/>
          </Link>
        </Navbar.Header>

        <HighlightedStatsPanelContainer />
        
        <Nav pullRight>
          { version && <NavItem eventKey={3} disabled>Version: {version}</NavItem> }
          <NavDropdown eventKey="1" title={<i className="fa fa-list fa-fw"></i>} id="task-alert-dropdown">
            <TaskAlertsContainer eventKey="1"/>
          </NavDropdown>
          <NavDropdown  eventKey="2" title={<span>{customerEmail} <i className="fa fa-user fa-fw"></i></span>} id="profile-dropdown">
            <LinkContainer to="/profile">
              <MenuItem eventKey="2.1">
                <i className="fa fa-user fa-fw"></i>Profile
            </MenuItem>
            </LinkContainer>
            <LinkContainer to="/login">
              <MenuItem eventKey="2.2" id="logoutLink" onClick={this.handleLogout}>
                <i className="fa fa-sign-out fa-fw"></i>Logout
            </MenuItem>
            </LinkContainer>
          </NavDropdown>
        </Nav>
      </Navbar>
    );
  }
}
