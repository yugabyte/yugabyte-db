// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import NumericInput from 'react-numeric-input';
import { isValidFunction } from 'utils/ObjectUtils';
import { YBLabel } from 'components/common/descriptors';

// TODO: Rename to YBNumericInput after changing prior YBNumericInput references.
// TODO: Make default export after checking all corresponding imports.

export class YBControlledNumericInput extends Component {
  static defaultProps = {
    minVal: 0,
    maxVal: 32,
  };
  render() {
    const {input, val, onInputChanged, onInputSelect, onInputBlur, onInputFocus, valueFormat} = this.props;
    return (
      <NumericInput {...input} className="form-control" value={val} onChange={onInputChanged}
                    onSelect={onInputSelect} onFocus={onInputFocus} onBlur={onInputBlur} format={valueFormat}/>
    )
  }
}
export class YBUnControlledNumericInput extends Component {
  static defaultProps = {
    minVal: 3,
    maxVal: 32,
  };

  render() {
    const { input, onInputChanged, minVal, maxVal } = this.props;

    function onChange(value) {
      input.onChange(value);
      if (isValidFunction(onInputChanged)) {
        onInputChanged(value);
      }
    }

    return (
      <NumericInput {...input} className="form-control" min={minVal} max={maxVal} onChange={onChange}/>
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
