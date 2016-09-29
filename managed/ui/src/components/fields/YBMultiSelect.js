// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Select from 'react-select';
import 'react-select/dist/react-select.css';
import YBLabel from './YBLabel';
export default class YBSelect extends Component {

  render() {
    const { input, label, meta, options, multi,
       name} = this.props;
    return (
      <YBLabel label={label} meta={meta}>
          <Select {...input}
            name={name}
            options={options}
            multi={multi}
            onBlur={() => {}}
          />
      </YBLabel>
    )
  }
}
