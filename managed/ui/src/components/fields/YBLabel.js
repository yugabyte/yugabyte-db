// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class YBLabel extends Component {

  render() {
    const { label, meta: { touched, error, invalid } } = this.props;

    return (
      <div className={`form-group ${ touched && invalid ? 'has-error' : ''}`}>
        <label className="form-item-label">
          {label}
          {this.props.children}
          <div className="help-block">
            {touched && error && <span>{error}</span>}
          </div>
        </label>
      </div>


    )
  }
}
