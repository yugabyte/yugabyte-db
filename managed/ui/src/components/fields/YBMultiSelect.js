// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Select from 'react-select';
import 'react-select/dist/react-select.css';
import YBLabel from './YBLabel';
import {isValidArray} from '../../utils/ObjectUtils';
export default class YBSelect extends Component {

  componentWillReceiveProps(props) {
    // If AZ is changed from multi to single take only last selection
    if (this.props.multi !== props.multi && props.multi === false) {
      var currentSelection = this.props.input.value;
      if (isValidArray(currentSelection) && currentSelection.length > 0) {
        this.props.input.onChange(currentSelection.splice(-1, 1));
      }
    }
    // If provider has been changed reset region selection
    if (this.props.providerSelected !== props.providerSelected) {
      this.props.input.onChange([]);
    }
  }

  render() {
    const { input, label, meta, options, multi,
      name, selectValChanged} = this.props;
    var self = this;

    function onChange(val) {
      val = multi ? val: val.slice(-1);
      self.props.input.onChange(val);
      selectValChanged(val);
    }
    return (
      <YBLabel label={label} meta={meta}>
        <Select {...input}
          name={name}
          options={options}
          multi={true}
          onBlur={() => {}}
          onChange={onChange}
        />
      </YBLabel>
    )
  }
}
