// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import {isValidArray, isProperObject} from '../../../../utils/ObjectUtils';
import YBRadioButton from './YBRadioButton';
import { YBLabel } from '../../descriptors';

export default class YBRadioButtonBar extends Component {
  static propTypes = {
    onSelect: PropTypes.func.isRequired
  }
  constructor(props) {
    super(props);
    this.state = {fieldValue: 0};
    this.radioButtonChecked = this.radioButtonChecked.bind(this);
  }
  componentWillMount() {
    this.setState({fieldValue: this.props.initialValue});
  }

  radioButtonChecked(event) {
    const {onSelect} = this.props;
    this.setState({fieldValue: Number(event.target.value)})
    onSelect(event.target.value);
  }
  render() {
    const { input, options } = this.props;
    var self = this;
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
      value = Number(value);
      function getLabelClass() {
        return 'btn btn-default' + (self.state.fieldValue === value ? ' bg-orange' : '');
      }
      return (
        <YBRadioButton key={value} {...input} fieldValue={value} checkState={self.state.fieldValue === value}
          label={display} labelClass={getLabelClass()} onClick={self.radioButtonChecked} />
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
