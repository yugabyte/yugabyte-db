// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class YBInputField extends Component {
  render() {
    const { input, label, type, meta: { touched, error, invalid } } = this.props;
    return (
      <div className={`form-group ${ touched && invalid ? 'has-error' : ''}`}>
        <label className="control-label">{label}</label>
        <div>
          <input {...input} placeholder={label} type={type} className="form-control"/>
          <div className="help-block">
            {touched && error && <span>{error}</span>}
          </div>
        </div>
      </div>
    )
  }
}


