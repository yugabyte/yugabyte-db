// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {isValidObject} from '../../../../utils/ObjectUtils';

export default class YBRadioButton extends Component {

  render() {
    const { input, checkState, fieldValue, label, labelClass, onClick } = this.props;
    var name = this.props.name || input.name;
    var id = this.props.id || `radio_button_${name}_${fieldValue}`;
    var onCheckClick = function(event) {
      input && input.onChange && input.onChange(event);
      return isValidObject(onClick) ? onClick(event) : true;
    }
    return (
      <label htmlFor={id} className={labelClass || 'radio-label'}>
        <input {...input} type="radio"
               id={id} name={name} value={fieldValue} defaultChecked={checkState}
               onClick={onCheckClick}
        />
        {label}
      </label>
    )
  }
}
