// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import '../stylesheets/SideNavBar.css'

export default class SideNavBar extends Component {
  constructor (props) {
    super(props);
    this.setFilter = this.setFilter.bind(this);
    this.isActive = this.isActive.bind(this);
  }

  componentWillMount () {
    this.setState({selected: ''});
  }

  setFilter (filter) {
    this.setState({selected : filter});
  }

  isActive(value) {
    if (value === this.state.selected) {
      return 'active';
    } else {
      return 'default';
    }
  }

	render() {
    return (
      <div className="col-md-3 left_col">
        <div className="left_col scroll-view">
          <div className="navbar nav_title">
            <a href="index.html" className="site_title"><i className="fa fa-database"></i> <span>YugaByte</span></a>
          </div>
          <div className="clearfix"></div>
          <br />
          <div id="sidebar-menu" className="main_menu_side hidden-print main_menu">
            <div className="menu_section">
              <ul className="nav side-menu">
                <li className={this.isActive('')} onClick={this.setFilter.bind(this, '')}>
                  <a href="#" ><i className="fa fa-home"></i> Home
                    <span className="label label-success"></span>
                  </a>
                </li>
                <li className={this.isActive('universe')} onClick={this.setFilter.bind(this, 'universe')}>
                  <a href="#"><i className="fa fa-globe"></i> Universes <span className="label label-success"></span></a>
                </li>
                <li className={this.isActive('alerts')} onClick={this.setFilter.bind(this, 'alerts')}>
                  <a href="#"><i className="fa fa-bell-o"></i> Alerts <span className="label label-success"></span></a>
                </li>
                <li className={this.isActive('metrics')} onClick={this.setFilter.bind(this, 'metrics')}><a href="#">
                  <i className="fa fa-line-chart"></i> Metrics <span className="label label-success"></span></a>
                </li>
                <li className={this.isActive('docs')} onClick={this.setFilter.bind(this, 'docs')}>
                  <a href="#"><i className="fa fa-file-text-o"></i> Docs <span className="label label-success"></span></a>
                </li>
                <li className={this.isActive('support')} onClick={this.setFilter.bind(this, 'support')}>
                  <a href="#"><i className="fa fa-phone"></i> Support <span className="label label-success"></span></a>
                </li>
              </ul>
            </div>
          </div>
        </div>
      </div>
  	);
  }
}
