// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import 'react-fa';
import { MenuItem , NavDropdown, Navbar, Nav } from 'react-bootstrap';
import { Link } from 'react-router';
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
    const { customer: { currentCustomer } } = this.props;
    const customerEmail = getPromiseState(currentCustomer).isSuccess()
        ? currentCustomer.data.email
        : "";

    return (
      <Navbar fixedTop>
        <Navbar.Header>
          <Link to="/" className="left_col text-center">
            <YBLogo />
          </Link>
        </Navbar.Header>

        <div className="flex-grow">
        </div>
        
        <Nav pullRight>
          <NavDropdown  eventKey="2" title={<span><i className="fa fa-user fa-fw"></i> {customerEmail}</span>} id="profile-dropdown">
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
