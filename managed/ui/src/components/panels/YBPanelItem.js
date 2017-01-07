// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import './stylesheets/YBPanelItem.css'

export default class YBPanelItem extends Component {

  render() {
    const { name, children} = this.props;
    var headerTextClass = "";
    return (
      <div>
        <div className="row">
          <div className="col-md-12">
            <div className="x_panel">
              <div className="x_title">
                <h2 className={headerTextClass}>{name}</h2>
                <div className="clearfix"></div>
              </div>
              {children}
            </div>
          </div>
        </div>
      </div>
    );
  }
}

YBPanelItem.defaultProps ={
  hideToolBox: false
}
