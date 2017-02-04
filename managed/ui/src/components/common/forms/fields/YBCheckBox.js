// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import {isValidObject} from '../../../../utils/ObjectUtils';

export default class YBCheckBox extends Component {

  render() {
    const { input, label, checkState,
            onClick } = this.props;
    var onCheckClick = function(event) {
      input.onChange(event);
      if (isValidObject(onClick)) {
        onClick(event);
      }
    }
    return (
      <label htmlFor={this.props.name}>
        <input {...input} type="checkbox"
          name={this.props.name} defaultChecked={checkState}
          id={this.props.id} onClick={onCheckClick} />
        &nbsp; {label}
      </label>
    );
  }
}
