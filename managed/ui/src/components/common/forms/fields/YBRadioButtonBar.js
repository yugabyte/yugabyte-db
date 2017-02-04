// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {isValidArray, isValidObject, isProperObject} from '../../../../utils/ObjectUtils';
import YBRadioButton from './YBRadioButton';

import { YBLabel } from '../../descriptors';

export default class YBRadioButtonBar extends Component {
  componentWillMount() {
    this.fieldValue = this.props.fieldValue;
  }

  componentWillReceiveProps(newProps) {
    this.fieldValue = newProps.fieldValue;
  }

  render() {
    const { input, options, onValueChanged, onClick } = this.props;
    var fieldValue = this.props.fieldValue;
    var onCheckClick = function(event) {
      input.onChange(event);
      if (isValidObject(onClick)) {
        onClick(event);
      }
    }
    var component = this;

    function radioButtonForOption(option) {
      var value, display;
      if (isValidArray(option)) {
        [value, display] = option;
      } else if (isProperObject(option)) {
        value = option.value;
        display = option.display;
      } else {
        value = display = option;
      }
      value = value.toString();
      function getLabelClass() {
        return 'btn btn-default' + (component.fieldValue === value ? ' bg-orange' : '');
      }
      var onClickWrapper = function (event) {
        component.fieldValue = event.target.value;
        component.forceUpdate();
        return isValidObject(onClick) ? onClick(event) : true;
      };
      return (
        <YBRadioButton key={value} {...input} fieldValue={value} checkState={component.fieldValue === value}
          label={display} labelClass={getLabelClass()} onClick={onClickWrapper} />
      );
    }
    return (
      <div className="btn-group" data-toggle="buttons">
        {options.map(radioButtonForOption)}
      </div>
    );
  }
}

export class YBRadioButtonBarWithLabel extends Component {
  render() {
    const { label, meta, ...otherProps} = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <YBRadioButtonBar {...otherProps} />
      </YBLabel>
    );
  }
}
