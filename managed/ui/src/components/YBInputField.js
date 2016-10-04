// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {FormControl} from 'react-bootstrap';
import YBLabel from './fields/YBLabel';

export default class YBInputField extends Component {
  render() {
    const { input, label, type, meta, className, placeHolder } = this.props;
    return (
      <YBLabel label={label} meta={meta}>
          <FormControl {...input} placeholder={placeHolder} type={type}
                            className={className} />
      </YBLabel>
    )
  }
}



