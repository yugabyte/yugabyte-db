// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isDefinedNotNull, isNonEmptyObject } from 'utils/ObjectUtils';

export default class YugawareLogs extends Component {
  componentWillMount() {
    this.props.getLogs();
  }

  render() {
    const { customer } = this.props;
    let logContent = <span />;
    if (isDefinedNotNull(customer.yugaware_logs) && isNonEmptyObject(customer.yugaware_logs)) {
      logContent = (
        <pre style={{"whiteSpace": "pre-wrap"}}>
          { customer.yugaware_logs.join('\n') }
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
