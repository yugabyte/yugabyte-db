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
          <div className="col-lg-10 universe-table-header">
            <h3>Universes<small> Status and details</small></h3>
          </div>
        <div className="col-lg-1 universe-table-header-action">
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
