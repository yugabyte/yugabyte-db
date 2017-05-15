// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { FormControl } from 'react-bootstrap';
import { isFunction } from 'lodash';
import { YBLabel } from 'components/common/descriptors';

// TODO: Make default export after checking all corresponding imports.
export class YBTextInput extends Component {
  static defaultProps = {
    isReadOnly: false
  };

  render() {
    var self = this;
    const { input, type, className, placeHolder, onValueChanged, isReadOnly } = this.props;

    function onChange(event) {
      if (isFunction(onValueChanged)) {
        onValueChanged(event.target.value);
      }
      self.props.input.onChange(event.target.value);
    }

    return (
      <FormControl {...input} placeholder={placeHolder} type={type} className={className}
        onChange={onChange} readOnly={isReadOnly} />
    );
  }
}

export default class YBTextInputWithLabel extends Component {
  render() {
    const { label, meta, ...otherProps } = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <YBTextInput {...otherProps} />
      </YBLabel>
    );
  }
}

export class YBControlledTextInput extends Component {
  render() {
    const { label, meta, input, type, className, placeHolder, onValueChanged, isReadOnly, val } = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <FormControl {...input} placeholder={placeHolder} type={type} className={className}
                     onChange={onValueChanged} readOnly={isReadOnly} value={val}/>
      </YBLabel>
    )
  }
}

// TODO: Deprecated. Rename all YBInputField references to YBTextInputWithLabel.
export var YBInputField = YBTextInputWithLabel;
