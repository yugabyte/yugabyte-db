// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import NumericInput from 'react-numeric-input';
import { isFunction } from 'lodash';
import { YBLabel } from 'components/common/descriptors';

// TODO: Rename to YBNumericInput after changing prior YBNumericInput references.
// TODO: Make default export after checking all corresponding imports.

export class YBControlledNumericInput extends Component {
  static defaultProps = {
    minVal: 0,
  };
  render() {
    const {input, val, onInputChanged, onInputSelect, onInputBlur, onInputFocus, valueFormat, minVal} = this.props;
    return (
      <NumericInput {...input} className="form-control" value={val} onChange={onInputChanged}
                    onSelect={onInputSelect} onFocus={onInputFocus} onBlur={onInputBlur} format={valueFormat}
                    min={minVal} />
    );
  }
}
export class YBUnControlledNumericInput extends Component {
  static defaultProps = {
    minVal: 0,

  };

  render() {
    const { input, onInputChanged, minVal } = this.props;

    function onChange(value) {
      input.onChange(value);
      if (isFunction(onInputChanged)) {
        onInputChanged(value);
      }
    }

    return (
      <NumericInput {...input} className="form-control" min={minVal} onChange={onChange}/>
    );
  }
}

export default class YBNumericInputWithLabel extends Component {
  render() {
    const { label, meta, ...otherProps } = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <YBUnControlledNumericInput {...otherProps} />
      </YBLabel>
    );
  }
}

export class YBControlledNumericInputWithLabel extends Component {
  render() {
    const { label, meta, onLabelClick, ...otherProps } = this.props;
    return (
      <YBLabel label={label} meta={meta} onLabelClick={onLabelClick}>
        <YBControlledNumericInput {...otherProps} />
      </YBLabel>
    );
  }
}

// TODO: Rename all prior YBNumericInput references to YBNumericInputWithLabel.
export var YBNumericInput = YBNumericInputWithLabel;
