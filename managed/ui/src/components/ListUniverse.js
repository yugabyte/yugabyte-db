// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import UniverseTableContainer from '../containers/UniverseTableContainer';
import YBPanelItem from './YBPanelItem';
import UniverseModalContainer from '../containers/UniverseModalContainer';

export default class ListUniverse extends Component {
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
          <div>
            <YBPanelItem name="Universe List">
              <UniverseTableContainer />
            </YBPanelItem>
          </div>
        </div>
      </div>
    )
  }
}
