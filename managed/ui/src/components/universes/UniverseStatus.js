// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import './stylesheets/UniverseStatus.scss';

export default class UniverseStatus extends Component {
  render() {
    const { universe, showLabelText } = this.props;
    var statusText = '';
    // TODO: Add other statuses here.
    var statusClassName = 'good';
    if (showLabelText) {
      // TODO: Add other statuses here.
      statusText = 'Ready';
    }

    return (
      <div className={'universe-status ' + statusClassName}>
        <i className="fa fa-check-circle" />
        {statusText && <span>{statusText}</span>}
      </div>
    );
  }
}
