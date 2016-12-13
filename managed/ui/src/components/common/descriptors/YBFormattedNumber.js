// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { FormattedNumber } from 'react-intl';
import './stylesheets/YBLabel.css'

export default class YBFormattedNumber extends Component {
  render() {
    return (
      <FormattedNumber {...this.props} style={this.props.formattedNumberStyle}/>
    )
  }
}
