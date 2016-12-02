// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import TopNavBarContainer from '../../containers/dashboard/TopNavBarContainer';
import SideNavBar from './SideNavBar';
import './stylesheets/NavBar.css'

export default class NavBar extends Component {
	render() {
		return (
			<div className="nav-sm yb-nav-bar">
				<div className="container body">
				  <div className="main-container">
				    <TopNavBarContainer />
				    <SideNavBar />
			    </div>
		    </div>
			</div>
		);
	}
}
