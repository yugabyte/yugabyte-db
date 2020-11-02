// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { isValidObject } from '../../../../utils/ObjectUtils';

export default class YBCheckBox extends Component {
  render() {
    const { input, label, checkState, onClick } = this.props;
    const onCheckClick = function (event) {
      if (input && input.onChange) {
        input.onChange(event);
      }
      if (isValidObject(onClick)) {
        onClick(event);
      }
    };
    return (
      <label htmlFor={this.props.name}>
        <input
          className="yb-input-checkbox"
          {...input}
          type="checkbox"
          name={this.props.name}
          defaultChecked={checkState}
          id={this.props.id}
          onClick={onCheckClick}
        />
        {label}
      </label>
    );
  }
}
