// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isFunction } from 'lodash';
import Select from 'react-select';
import 'react-select/dist/react-select.css';

import { YBLabel } from 'components/common/descriptors';
import { isNonEmptyArray } from 'utils/ObjectUtils';

// TODO: Rename to YBMultiSelect after changing prior YBMultiSelect references.
// TODO: Make default export after checking all corresponding imports.
export class YBNewMultiSelect extends Component {
  componentWillReceiveProps(props) {
    let newSelection = null;

    // If AZ is changed from multi to single, take only last selection.
    if (this.props.multi !== props.multi && props.multi === false) {
      const currentSelection = this.props.input.value;
      if (isNonEmptyArray(currentSelection)) {
        newSelection = currentSelection.splice(-1, 1);
      }
    }
    // If provider has been changed, reset region selection.
    if (this.props.providerSelected !== props.providerSelected) {
      newSelection = [];
    }

    if (isNonEmptyArray(newSelection) && isFunction(this.props.input.onChange)) {
      this.props.input.onChange(newSelection);
    }
  }

  render() {
    const { input, options, multi, selectValChanged } = this.props;
    const self = this;

    function onChange(val) {
      val = multi ? val: val.slice(-1);
      if (isFunction(self.props.input.onChange)) {
        self.props.input.onChange(val);
      }
      if (selectValChanged) {
        selectValChanged(val);
      }
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
export const YBMultiSelect = YBMultiSelectWithLabel;
