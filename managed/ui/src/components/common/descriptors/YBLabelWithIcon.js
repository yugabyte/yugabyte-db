// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';

export default class YBLabelWithIcon extends Component {
  static propTypes = {
    icon: PropTypes.string
  };
  render() {
    const { icon, className } = this.props;
    return (
      <span className={className}>
        {icon && <i className={icon}></i>}
        {this.props.children}
      </span>
    );
  }
}
