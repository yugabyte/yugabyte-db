// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link, IndexLink, withRouter } from 'react-router';
import { NavDropdown } from 'react-bootstrap';
import gitterIcon from '../../help/HelpItem/images/gitter.svg';
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
        <div className="left_col" >
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
                  <NavDropdown dropup eventKey="2" title={<div><i className="fa fa-question"></i><span>Help</span></div>} id="help-dropdown">
                    <h4>Resources</h4>
                    <li>
                      <a href="https://docs.yugabyte.com/" target="_blank" rel="noopener noreferrer">
                        <i className="fa fa-book"></i> Documentation
                      </a>
                    </li>
                    <li>
                      <a href="https://github.com/yugabyte" target="_blank" rel="noopener noreferrer">
                        <i className="fa fa-github"></i> GitHub
                      </a>
                    </li>
                    <h4>Support</h4>
                    <li>
                      <a href="https://gitter.im/YugaByte/Lobby" target="_blank" rel="noopener noreferrer">
                        <object data={gitterIcon} type="image/svg+xml" width="16">Icon</object> Gitter
                      </a>
                    </li>
                    <li>
                      <a href="https://www.youtube.com/channel/UCL9BhSLRowqQ1TyBndhiCEw" target="_blank" rel="noopener noreferrer">
                        <i className="fa fa-youtube"></i> YouTube
                      </a>
                    </li>
                    <li>
                      <a href="mailto:support@yugabyte.com"><i className="fa fa-envelope-o"></i> Email</a>
                    </li>
                  </NavDropdown>
                </ul>
              </div>
            </div>
          </div>
        </div>
      </div>
    );
  }
}
