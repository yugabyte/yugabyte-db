// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isDefinedNotNull, isNonEmptyObject } from 'utils/ObjectUtils';
import { browserHistory} from 'react-router';
import { isNonAvailable } from 'utils/LayoutUtils';

export default class YugawareLogs extends Component {
  componentWillMount() {
    this.props.getLogs();

    const { customer: { currentCustomer }} = this.props;
    if (isNonAvailable(currentCustomer.data.features, "main.logs")) browserHistory.push('/');
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
