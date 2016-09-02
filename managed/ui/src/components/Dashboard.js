// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import DashboardRightPane from './DashboardRightPane';
import UniverseModalContainer from '../containers/UniverseModalContainer';

export default class Dashboard extends Component {
  static contextTypes = {
    router: PropTypes.object
  };

  render() {
    return (
      <div id="page-wrapper">
        <div className="row header-row">
          <div className="col-lg-9 universeTableHeader">
            <span className="h3">Universes</span>
            <small> Status And Details</small>
          </div>
          <div className="col-lg-2">
            <UniverseModalContainer type="Create" />
          </div>
        </div>
        <div>
          <DashboardRightPane {...this.props}/>
        </div>
      </div>
    );
    }
  }
