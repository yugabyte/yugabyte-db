// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link, IndexLink, withRouter } from 'react-router';
import './stylesheets/SideNavBar.scss';

class NavLink extends Component {
  render () {
    const { router, index, to, children, icon, ...props } = this.props;

    // Added by withRouter in React Router 3.0.
    delete props.params;
    delete props.location;
    delete props.routes;
    let isActive = router.isActive(to, index);
    const LinkComponent = index ?  IndexLink : Link;

    return (
      <li className={isActive ? 'active' : ''}>
        <LinkComponent to={to} {...props}><i className={icon}></i>{children}</LinkComponent>
      </li>
    )
  }
}

NavLink = withRouter(NavLink);


export default class SideNavBar extends Component {

  render() {
    return (
      <div className="side-nav-container">
        <div className="col-md-3 left_col" >
          <div className="left_col scroll-view">
            <div id="sidebar-menu" className="main_menu_side hidden-print main_menu">
              <div className="menu_section">
                <ul className="nav side-menu">
                  <NavLink to="/" index={true} icon="fa fa-dashboard">
                    Dashboard
                  </NavLink>
                  <NavLink to="/universes" icon="fa fa-globe">
                    Universes
                  </NavLink>
                  <NavLink to="/metrics" icon="fa fa-line-chart">
                    Metrics
                  </NavLink>
                  <NavLink to="/tasks" icon="fa fa-list">
                    Tasks
                  </NavLink>
                  <NavLink to="/alerts" icon="fa fa-bell-o">
                    Alerts
                  </NavLink>
                  <NavLink to="/config" icon="fa fa-cloud-upload">
                    Configuration
                  </NavLink>
                </ul>
                <ul className="nav side-menu position-bottom">
                  <NavLink to="/help" icon="fa fa-question-circle-o">
                    Help
                  </NavLink>
                </ul>
              </div>
            </div>
          </div>
        </div>
      </div>
    );
  }
}
