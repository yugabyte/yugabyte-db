// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Link } from 'react-router';

export default class YBBreadcrumb extends Component {
  static propTypes = {
    icon: PropTypes.string.isRequired,
  };

  render() {
    return (
      <span>
        <Link {...this.props}>
          {this.props.children}
        </Link>
        <i className="fa fa-angle-right fa-fw"></i>
      </span>
    );
  }
}
