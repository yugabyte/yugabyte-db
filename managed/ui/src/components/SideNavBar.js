// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';

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
      <div className="left-col-container">
        <div className="col-md-3 left_col" >
          <div className="left_col scroll-view">
            <div id="sidebar-menu" className="main_menu_side hidden-print main_menu">
              <div className="menu_section">
                <ul className="nav side-menu">
                  <li className={this.isActive('')} onClick={this.setFilter.bind(this, '')}>
                    <Link to="/dashboard" ><i className="fa fa-home"></i> Home
                      <span className="label label-success"></span>
                    </Link>
                  </li>
                  <li className={this.isActive('universe')} onClick={this.setFilter.bind(this, 'universe')}>
                    <Link to="/universes"><i className="fa fa-globe"></i> Universes
                      <span className="label label-success"></span>
                    </Link>
                  </li>
                  <li className={this.isActive('alerts')} onClick={this.setFilter.bind(this, 'alerts')}>
                    <Link to="/alerts"><i className="fa fa-bell-o"></i> Alerts <span className="label label-success"></span></Link>
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
      </div>
    );
  }
}
