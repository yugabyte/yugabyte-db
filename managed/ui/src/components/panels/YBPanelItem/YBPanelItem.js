// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import './YBPanelItem.css';

export default class YBPanelItem extends Component {

  render() {
    const { name, children} = this.props;
    const headerTextClass = "";
    return (
      <div>
        <div className="x_panel">
          <div className="x_title clearfix">
            <h3 className={headerTextClass}>{name}</h3>
          </div>
          {children}
        </div>
      </div>
    );
  }
}

YBPanelItem.defaultProps ={
  hideToolBox: false
};
