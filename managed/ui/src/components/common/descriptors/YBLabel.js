// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import './stylesheets/YBLabel.scss'

export default class YBLabel extends Component {
  render() {
    const { label, meta: { touched, error, invalid }, onLabelClick } = this.props;

    return (
      <div className={`form-group ${ touched && invalid ? 'has-error' : ''}`} onClick={onLabelClick}>
        <label className="form-item-label">
          {label}
        </label>
        <div className="yb-field-group">
          {this.props.children}
          <div className="help-block">
            {touched && error && <span>{error}</span>}
          </div>
        </div>
      </div>
    )
  }
}
