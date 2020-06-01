// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { FormControl } from 'react-bootstrap';
import { isFunction } from 'lodash';
import { YBLabel } from '../../../../components/common/descriptors';

export default class YBTextArea extends Component {
  static defaultProps = {
    isReadOnly: false
  };
  render() {
    const self = this;
    const { input, type, className, placeHolder, onValueChanged, isReadOnly, label, meta, insetError, infoContent,
      infoTitle, infoPlacement } = this.props;
    function onChange(event) {
      if (isFunction(onValueChanged)) {
        onValueChanged(event.target.value);
      }
      self.props.input.onChange(event.target.value);
    }
    return (
      <YBLabel label={label} insetError={insetError} meta={meta} infoContent={infoContent} infoTitle={infoTitle}
               infoPlacement={infoPlacement}>
        <FormControl {...input} componentClass="textarea" placeholder={placeHolder} type={type} className={className}
                   onChange={onChange} readOnly={isReadOnly} />
      </YBLabel>
    );
  }
}
