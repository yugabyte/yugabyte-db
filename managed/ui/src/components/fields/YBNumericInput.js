// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import YBLabel from './YBLabel';
import NumericInput from 'react-numeric-input';

export default class YBNumericInput extends Component {
  render() {
    const { input, label, meta} = this.props;
    return (
      <YBLabel label={label} meta={meta}>
          <NumericInput {...input}
            className="form-control" min={3} max={32} />
      </YBLabel>
    )
  }
}
