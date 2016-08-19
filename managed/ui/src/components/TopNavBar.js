// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import '../stylesheets/TopNavBar.css';
import 'react-fa'

export default class TopNavBar extends Component {
	render() {
		return (
			<ul className="nav navbar-top-links navbar-right">
				<li className="dropdown">
					<a className="dropdown-toggle" data-toggle="dropdown" href="#">
						<i className="fa fa-user fa-fw"></i>  <i className="fa fa-caret-down"></i>
					</a>
					<ul className="dropdown-menu dropdown-user">
						<li><a href="/profile"><i className="fa fa-user fa-fw"></i> Profile</a></li>
						<li className="divider"></li>
						<li><a href="/logout" id="logoutLink"><i className="fa fa-sign-out fa-fw"></i> Logout</a></li>
					</ul>
				</li>
			</ul>
		);
	}
}
