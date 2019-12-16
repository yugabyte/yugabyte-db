// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isDefinedNotNull, isNonEmptyObject } from 'utils/ObjectUtils';
import { showOrRedirect } from 'utils/LayoutUtils';

export default class YugawareLogs extends Component {
  componentDidMount() {
    this.props.getLogs();
  }

  render() {
    const { customer: { currentCustomer, yugaware_logs} } = this.props;
    showOrRedirect(currentCustomer.data.features, "main.logs");

    let logContent = <span />;
    if (isDefinedNotNull(yugaware_logs) && isNonEmptyObject(yugaware_logs)) {
      logContent = (
        <pre style={{"whiteSpace": "pre-wrap"}}>
          { yugaware_logs.join('\n') }
        </pre>
      );
    }
    return (
      <div>
        <h2><b>YugaWare logs</b></h2>
        <div>
          {logContent}
        </div>
      </div>
    );
  }
}
