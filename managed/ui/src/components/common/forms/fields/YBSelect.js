// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isValidFunction } from 'utils/ObjectUtils';
import { YBLabel } from 'components/common/descriptors';

// TODO: Rename to YBSelect after changing prior YBSelect references.
// TODO: Make default export after checking all corresponding imports.
export class YBNewSelect extends Component {
  render() {
    const { input, options, onSelectChange, readOnlySelect } = this.props;

    function onChange(event) {
      input.onChange(event.target.value);
      if (isValidFunction(onSelectChange)) {
        onSelectChange(event.target.value);
      }
    }

    return (
      <select {...input} className="form-control" disabled={readOnlySelect} onChange={onChange}>
        {options}
      </select>
    );
  }
}

export default class YBSelectWithLabel extends Component {
  render() {
    const { label, meta, ...otherProps} = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <YBNewSelect {...otherProps} />
      </YBLabel>
    );
  }
}

// TODO: Rename all prior YBSelect references to YBSelectWithLabel.
export var YBSelect = YBSelectWithLabel;
