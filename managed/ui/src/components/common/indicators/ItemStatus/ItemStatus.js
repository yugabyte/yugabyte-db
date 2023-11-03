// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import './ItemStatus.scss';

export default class ItemStatus extends Component {
  render() {
    const { showLabelText } = this.props;
    let statusText = '';
    // TODO: Add other statuses here.
    const statusClassName = 'good';
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
