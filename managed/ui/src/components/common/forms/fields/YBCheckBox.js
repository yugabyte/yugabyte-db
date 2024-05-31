// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import clsx from 'clsx';

import { isValidObject } from '../../../../utils/ObjectUtils';

export default class YBCheckBox extends Component {
  render() {
    const { input, field, label, checkState, onClick, disabled, dataTestId } = this.props;
    const onCheckClick = (event) => {
      if (input?.onChange) {
        input.onChange(event);
      }
      if (field?.onChange) {
        field.onChange(event);
      }
      if (isValidObject(onClick)) {
        onClick(event);
      }
    };
    return (
      <label htmlFor={this.props.name}>
        <span className="yb-input-checkbox">
          <input
            className="yb-input-checkbox__input"
            {...input}
            type="checkbox"
            name={field?.name}
            data-testid={dataTestId}
            defaultChecked={checkState}
            id={this.props.id}
            onClick={onCheckClick}
            disabled={disabled}
          />
          <span className={clsx('yb-input-checkbox__inner', disabled && 'disabled')}></span>
        </span>
        <span style={{ marginLeft: '6px', verticalAlign: 'middle' }}>{label}</span>
      </label>
    );
  }
}
