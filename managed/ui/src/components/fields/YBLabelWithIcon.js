// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

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

