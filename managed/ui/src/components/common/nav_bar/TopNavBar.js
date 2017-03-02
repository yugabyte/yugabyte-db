// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import 'react-fa';
import { MenuItem , NavDropdown, Navbar, Nav, Image } from 'react-bootstrap';
import { Link } from 'react-router';
import { HighlightedStatsPanelContainer } from '../../panels';
import { TaskAlertsContainer } from '../../tasks';
import logo from './images/small-logo.png';
import './stylesheets/TopNavBar.scss';

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
		return (
			<Navbar fixedTop>
				<Navbar.Header>
					<Link to="/" className="col-md-3 left_col text-center">
						<Image src={logo} className="yb-logo-img" />
					</Link>
				</Navbar.Header>
				<Nav pullRight>
					<NavDropdown eventKey="1" title={<i className="fa fa-list fa-fw"></i>} id="task-alert-dropdown">
						<TaskAlertsContainer eventKey="1"/>
					</NavDropdown>
					<NavDropdown eventKey="2" title={<i className="fa fa-user fa-fw"></i>} id="profile-dropdown">
						<MenuItem eventKey="2.1" href="/profile">
							<i className="fa fa-user fa-fw"></i>Profile
						</MenuItem>
						<MenuItem eventKey="2.2" href="/login" id="logoutLink" onClick={this.handleLogout}>
							<i className="fa fa-sign-out fa-fw"></i>Logout
						</MenuItem>
					</NavDropdown>
				</Nav>

        <HighlightedStatsPanelContainer />
			</Navbar>
		);
	}
}
