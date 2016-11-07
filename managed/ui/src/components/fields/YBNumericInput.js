// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import YBLabel from './YBLabel';
import NumericInput from 'react-numeric-input';

export default class YBNumericInput extends Component {
  render() {
    const { input, label, meta, onValueChanged} = this.props;
    var onChange = function(value) {
      input.onChange(value);
      onValueChanged(value);
    }
    return (
      <YBLabel label={label} meta={meta}>
          <NumericInput {...input}
            className="form-control" min={3} max={32} onChange={onChange}/>
      </YBLabel>
    )
  }
}
