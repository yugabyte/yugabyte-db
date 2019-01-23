// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBLabel } from 'components/common/descriptors';
import { FormControl } from 'react-bootstrap';

export default class YBFormInput extends Component {
  render() {
    const { insetError, ...rest } = this.props;

    return (
      <YBLabel {...this.props} >
        <FormControl
          {...this.props.field}
          {...rest}
        />
      </YBLabel>
    );
  }
}
