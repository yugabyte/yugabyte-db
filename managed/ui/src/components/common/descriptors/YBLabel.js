// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class YBLabel extends Component {
  render() {
    const { label, meta: { touched, error, invalid }, onLabelClick } = this.props;

    return (
      <div className={`form-group ${ touched && invalid ? 'has-error' : ''}`} onClick={onLabelClick}>
        {label &&
          <label className="form-item-label">
            {label}
          </label>
        }
        <div className="yb-field-group">
          {this.props.children}
          {touched && error &&
            <div className="help-block">
              <span>{error}</span>
            </div>
          }
        </div>
      </div>
    )
  }
}
