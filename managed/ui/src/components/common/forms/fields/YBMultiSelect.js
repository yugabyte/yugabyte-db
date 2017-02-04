// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Select from 'react-select';
import 'react-select/dist/react-select.css';

import { YBLabel } from 'components/common/descriptors';
import { isValidArray, isValidFunction } from 'utils/ObjectUtils';

// TODO: Rename to YBMultiSelect after changing prior YBMultiSelect references.
// TODO: Make default export after checking all corresponding imports.
export class YBNewMultiSelect extends Component {
  componentWillReceiveProps(props) {
    var newSelection = null;

    // If AZ is changed from multi to single, take only last selection.
    if (this.props.multi !== props.multi && props.multi === false) {
      var currentSelection = this.props.input.value;
      if (isValidArray(currentSelection) && currentSelection.length > 0) {
        newSelection = currentSelection.splice(-1, 1);
      }
    }
    // If provider has been changed, reset region selection.
    if (this.props.providerSelected !== props.providerSelected) {
      newSelection = [];
    }

    if (isValidArray(newSelection) && isValidFunction(this.props.input.onChange)) {
      this.props.input.onChange(newSelection);
    }
  }

  render() {
    const { input, options, multi, selectValChanged } = this.props;
    var self = this;

    function onChange(val) {
      val = multi ? val: val.slice(-1);
      if (isValidFunction(self.props.input.onChange)) {
        self.props.input.onChange(val);
      }
      selectValChanged(val);
    }

    return (
      <Select {...input} options={options} multi={true} onBlur={() => {}} onChange={onChange} />
    );
  }
}

export default class YBMultiSelectWithLabel extends Component {
  render() {
    const { label, meta, ...otherProps} = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <YBNewMultiSelect {...otherProps} />
      </YBLabel>
    );
  }
}

// TODO: Rename all prior YBMultiSelect references to YBMultiSelectWithLabel.
export var YBMultiSelect = YBMultiSelectWithLabel;
