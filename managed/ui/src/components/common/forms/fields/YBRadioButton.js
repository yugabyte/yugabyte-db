// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {isValidObject} from '../../../../utils/ObjectUtils';

export default class YBRadioButton extends Component {

  render() {
    const { input, checkState, fieldValue,
      onClick } = this.props;
    var onCheckClick = function(event) {
      input.onChange(event);
      if (isValidObject(onClick)) {
        onClick(event);
      }
    }
    return (
      <input {...input} type="radio"
               name={this.props.name} defaultChecked={checkState}
               id={this.props.id} onClick={onCheckClick} value={fieldValue}
        />

    )
  }
}
