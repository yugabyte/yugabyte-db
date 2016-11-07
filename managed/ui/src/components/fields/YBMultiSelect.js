// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Select from 'react-select';
import 'react-select/dist/react-select.css';
import YBLabel from './YBLabel';
export default class YBSelect extends Component {

  render() {
    const { input, label, meta, options, multi,
            name, selectValChanged} = this.props;
    var self = this;
    
    function onChange(val) {
      self.props.input.onChange(val);
      selectValChanged(val);
    }
    return (
      <YBLabel label={label} meta={meta}>
        <Select {...input}
          name={name}
          options={options}
          multi={multi}
          onBlur={() => {}}
          onChange={onChange}
        />
      </YBLabel>
    )
  }
}
