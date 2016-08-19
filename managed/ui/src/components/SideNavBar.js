// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import '../stylesheets/SideNavBar.css'

export default class SideNavBar extends Component {
	render() {
    return (
      <div className="navbar-default sidebar" role="navigation">
        <div className="sidebar-nav navbar-collapse">
          <ul className="nav in" id="side-menu">
            <li>
              <a href="/" className="active"><i className="fa fa-dashboard fa-fw"></i> Dashboard</a>
            </li>
            <li className="">
              <a href="#"><i className="fa fa-database fa-fw"></i> Universes<span className="fa arrow"></span></a>
              <ul className="nav nav-second-level collapse instance-list" aria-expanded="false"></ul>
            </li>
          </ul>
        </div>
      </div>
  	);
  }
}
