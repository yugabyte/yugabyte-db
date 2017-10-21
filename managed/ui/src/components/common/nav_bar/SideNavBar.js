// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link, IndexLink, withRouter } from 'react-router';
import './stylesheets/SideNavBar.scss';

class NavLink extends Component {
  render () {
    const { router, index, to, icon, text, ...props } = this.props;

    // Added by withRouter in React Router 3.0.
    delete props.params;
    delete props.location;
    delete props.routes;
    const isActive = router.isActive(to, index);
    const LinkComponent = index ?  IndexLink : Link;

    return (
      <li className={isActive ? 'active' : ''}>
        <LinkComponent to={to} title={text} {...props}><i className={icon}></i><span>{text}</span></LinkComponent>
      </li>
    );
  }
}

NavLink = withRouter(NavLink);


export default class SideNavBar extends Component {

  render() {
    return (
      <div className="side-nav-container">
        <div className="col-md-3 left_col" >
          <div className="left_col scroll-view">
            <div id="sidebar-menu" className="main-menu-side hidden-print main-menu">
              <div className="menu_section">
                <ul className="nav side-menu">
                  <NavLink to="/" index={true} icon="fa fa-dashboard" text="Dashboard" />
                  <NavLink to="/universes" icon="fa fa-globe" text="Universes" />
                  <NavLink to="/metrics" icon="fa fa-line-chart" text="Metrics" />
                  <NavLink to="/tasks" icon="fa fa-list" text="Tasks" />
                  <NavLink to="/alerts" icon="fa fa-bell-o" text="Alerts" />
                  <NavLink to="/config" icon="fa fa-cloud-upload" text="Configuration" />
                </ul>
                <ul className="nav side-menu position-bottom">
                  <NavLink to="/help" icon="fa fa-question" text="Help" />
                </ul>
              </div>
            </div>
          </div>
        </div>
      </div>
    );
  }
}
