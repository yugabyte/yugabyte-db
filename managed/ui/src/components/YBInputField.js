// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import YBLabel from './fields/YBLabel';

export default class YBInputField extends Component {
  render() {
    const { input, label, type, meta } = this.props;
    return (
      <YBLabel label={label} meta={meta}>
          <input {...input} placeholder={label} type={type}
                            className="form-control"/>
      </YBLabel>
    )
  }
}


