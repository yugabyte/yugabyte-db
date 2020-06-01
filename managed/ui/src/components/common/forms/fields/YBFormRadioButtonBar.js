// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isObject } from 'lodash';
import { isNonEmptyArray, isDefinedNotNull } from '../../../../utils/ObjectUtils';
import YBFormRadioButton from './YBFormRadioButton';
import { YBLabel } from '../../descriptors';
import { Field } from 'formik';

class YBRadioButtonGroupDefault extends Component {
  render() {
    const { field, name, segmented, options, ...props } = this.props;
    return (
      <div className={"btn-group btn-group-radio"+ (segmented ? " btn-group-segmented" : "")} data-toggle="buttons">
        {options.map((option) => {
          let value, label;
          if (isNonEmptyArray(option)) {
            [value, label] = option;
          } else if (isObject(option)) {
            value = option.value;
            label = option.label;
          } else {
            value = label = option;
          }
          const key = name + "_" + value;
          return <Field component={YBFormRadioButton} key={key} id={value} name={name} label={label} segmented={segmented} {...props} />;
        })}
      </div>
    );
  }
}

export default class YBRadioButtonGroup extends Component {
  render() {
    const { label, meta, type, ...otherProps} = this.props;
    if (isDefinedNotNull(label)) {
      return (
        <YBLabel label={label} meta={meta}>
          <YBRadioButtonGroupDefault {...otherProps} />
        </YBLabel>
      );
    } else {
      return <YBRadioButtonGroupDefault {...otherProps} />;
    }
  }
}

export class YBSegmentedButtonGroup extends Component {
  render() {
    const { label, meta, type, ...otherProps} = this.props;
    if (isDefinedNotNull(label)) {
      return (
        <YBLabel label={label} meta={meta}>
          <YBRadioButtonGroupDefault segmented={true} {...otherProps} />
        </YBLabel>
      );
    } else {
      return <YBRadioButtonGroupDefault segmented={true} {...otherProps} />;
    }
  }
}
