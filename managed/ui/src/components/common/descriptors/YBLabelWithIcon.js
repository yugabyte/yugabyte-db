// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';

export default class YBLabelWithIcon extends Component {
  render() {
    const {icon} = this.props;
    return (
      <span>
          <i className={icon}></i>
          {this.props.children}
      </span>
    )
  }
}
YBLabelWithIcon.propTypes = {
  icon: PropTypes.string.isRequired
}

