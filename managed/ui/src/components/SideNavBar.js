// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import '../stylesheets/SideNavBar.css'

export default class SideNavBar extends Component {
	render() {
    return (
  		<div className="navbar-default sidebar">
	  	  <div className="sidebar-nav navbar-collapse">
					<ul className="nav in">
					  <li><a href="/" class="active"><i class="fa fa-dashboard fa-fw"></i> Dashboard</a></li>
					</ul>
		  	</div>
			</div>
	  );
  }
}
