// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import AlertsListContainer from '../containers/AlertsListContainer';

export default class Alerts extends Component {
  render() {
    return (
      <div>
        <AlertsListContainer/>
      </div>
    )
  }
}
