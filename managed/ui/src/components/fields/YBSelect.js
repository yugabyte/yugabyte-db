// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import YBLabel from './YBLabel';

export default class YBSelect extends Component {
  render() {
    const { input, label, meta, options, onChange,
      value, readOnlySelect, name, defaultValue} = this.props;
    return (
      <YBLabel label={label} meta={meta}>
          <select {...input} name={name} className="form-control"
                  disabled={readOnlySelect} onChange={onChange} value={value}
                  defaultValue={defaultValue}
                 >
            {options}
          </select>
      </YBLabel>
    )
  }
}
