// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import '../stylesheets/TopNavBar.css';
import 'react-fa';
import { MenuItem , NavDropdown } from 'react-bootstrap';

export default class TopNavBar extends Component {
	constructor(props) {
    super(props);
    this.handleLogout = this.handleLogout.bind(this);
  }

  handleLogout(event) {
    this.props.logoutProfile();
  }

	render() {
		return (
			<ul className="nav navbar-top-links navbar-right">
				<NavDropdown eventKey="4" title=
					{<div className='fa fa-user top-nav-menu-button'></div>} id="nav-dropdown">
				  <MenuItem eventKey="4.1" href="/profile"><i className="fa fa-user fa-fw"></i>
						&nbsp;Profile
					</MenuItem>
				  <MenuItem divider />
				  <MenuItem eventKey="4.2" href="/login" id="logoutLink" onClick={this.handleLogout}>
						<i className="fa fa-sign-out fa-fw"></i>
						Logout
					</MenuItem>
			  </NavDropdown>
      </ul>
		);
	}
}
